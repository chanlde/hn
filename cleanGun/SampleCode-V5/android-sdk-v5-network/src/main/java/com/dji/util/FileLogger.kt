package com.dji.util

import android.content.Context
import android.content.pm.ApplicationInfo
import android.os.Environment
import android.os.SystemClock
import android.util.Log
import java.io.File
import java.io.FileWriter
import java.io.IOException
import java.text.SimpleDateFormat
import java.util.Calendar
import java.util.Date
import java.util.Locale
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.locks.ReentrantLock
import kotlin.concurrent.withLock

/**
 * 文件日志系统
 *
 * **约定**
 * - 错误/警告：使用 [e] / [w]，消息应包含可定位信息（API、tid、错误码等）。
 * - 业务里程碑：使用 [i]（任务开始/结束、上传成功、RTMP 连上等）。
 * - 高频/调试细节：使用 [throttledD] 或 [logStateChange]，避免热路径刷屏写盘。
 *
 * 日志目录：`/storage/emulated/0/Download/AppLogs/`（或应用外置目录备用）
 */
object FileLogger {

    private const val TAG = "FileLogger"
    private const val LOG_DIR_NAME = "AppLogs"
    private const val LOG_FILE_PREFIX = "log_"
    private const val LOG_FILE_SUFFIX = ".txt"
    private const val MAX_LOG_DAYS = 7

    private var logDir: File? = null
    private val lock = ReentrantLock()
    private val dateFormat = SimpleDateFormat("yyyy-MM-dd HH:mm:ss.SSS", Locale.getDefault())
    private val fileDateFormat = SimpleDateFormat("yyyyMMdd", Locale.getDefault())
    private val launchTimestamp = SimpleDateFormat("yyyyMMdd_HHmmss", Locale.getDefault()).format(Date())

    /** 关闭后不再写入日志文件（仍会打 Logcat 的级别见各方法） */
    @Volatile
    var fileLoggingEnabled: Boolean = true

    /**
     * 仅当 `android.util.Log` 级别数值 **大于等于** 本值时才写入文件。
     * 例如 Release 可设为 [Log.INFO]，减少 DEBUG 刷盘；默认 [Log.DEBUG] 全量写文件。
     */
    @Volatile
    var minLevelForFile: Int = Log.DEBUG

    private var defaultUncaughtExceptionHandler: Thread.UncaughtExceptionHandler? = null
    private var isCrashHandlerRegistered = false

    private val throttleLastElapsed = ConcurrentHashMap<String, Long>()
    private val stateLastValue = ConcurrentHashMap<String, String>()

    private val uncaughtExceptionHandler = Thread.UncaughtExceptionHandler { thread, throwable ->
        val msg = buildString {
            append("未捕获崩溃 | 线程=${thread.name}[${thread.id}] | ")
            append("类型=${throwable.javaClass.name} | 消息=${throwable.message}")
        }
        e("CRASH", msg, throwable)
        flushLogs()
        defaultUncaughtExceptionHandler?.uncaughtException(thread, throwable)
    }

    /**
     * 根据 [Context] 设置常见默认：Debug 包全级别写文件；Release 仅 INFO 及以上写文件。
     */
    fun applyDefaultPolicy(context: Context) {
        val isDebug =
            (context.applicationInfo.flags and ApplicationInfo.FLAG_DEBUGGABLE) != 0
        minLevelForFile = if (isDebug) Log.DEBUG else Log.INFO
    }

    /**
     * 同一 [throttleKey] 在 [intervalMs] 内只记录一条（文件 + Logcat），用于轮询、GPS 等热路径。
     */
    @JvmOverloads
    fun throttledD(tag: String, throttleKey: String, message: String, intervalMs: Long = 5_000L) {
        val now = SystemClock.elapsedRealtime()
        val last = throttleLastElapsed[throttleKey] ?: 0L
        if (now - last < intervalMs) return
        throttleLastElapsed[throttleKey] = now
        d(tag, message)
    }

    /**
     * 仅当 [newValue] 与上次记录不同时打一条 DEBUG（含首次）。
     */
    fun logStateChange(tag: String, stateKey: String, newValue: Any?) {
        val serialized = newValue?.toString() ?: "null"
        val prev = stateLastValue.put(stateKey, serialized)
        if (prev == serialized) return
        d(tag, "$stateKey: ${prev ?: "<init>"} -> $serialized")
    }

    fun init(context: Context, enableCrashHandler: Boolean = true) {
        try {
            val downloadDir = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS)
            logDir = File(downloadDir, LOG_DIR_NAME)

            if (!logDir!!.exists()) {
                val created = logDir!!.mkdirs()
                if (!created) {
                    Log.e(TAG, "创建日志目录失败: ${logDir!!.absolutePath}")
                    val fallbackDir = context.getExternalFilesDir(null)
                    logDir = File(fallbackDir, LOG_DIR_NAME)
                    logDir!!.mkdirs()
                }
            }

            cleanOldLogs()
            applyDefaultPolicy(context)
            i(TAG, "日志初始化 目录=${logDir!!.absolutePath} minLevelForFile=$minLevelForFile")

            if (enableCrashHandler && !isCrashHandlerRegistered) {
                registerCrashHandler()
            }
        } catch (e: Exception) {
            Log.e(TAG, "初始化文件日志系统失败: ${e.message}", e)
        }
    }

    private fun registerCrashHandler() {
        try {
            defaultUncaughtExceptionHandler = Thread.getDefaultUncaughtExceptionHandler()
            Thread.setDefaultUncaughtExceptionHandler(uncaughtExceptionHandler)
            isCrashHandlerRegistered = true
            Log.d(TAG, "全局异常处理器已注册")
        } catch (e: Exception) {
            Log.e(TAG, "注册全局异常处理器失败: ${e.message}", e)
        }
    }

    fun unregisterCrashHandler() {
        try {
            if (isCrashHandlerRegistered) {
                Thread.setDefaultUncaughtExceptionHandler(defaultUncaughtExceptionHandler)
                isCrashHandlerRegistered = false
                Log.d(TAG, "全局异常处理器已取消注册")
            }
        } catch (e: Exception) {
            Log.e(TAG, "取消注册全局异常处理器失败: ${e.message}", e)
        }
    }

    private fun flushLogs() {
        lock.withLock { }
    }

    fun getLogDir(): File? = logDir

    private fun shouldWriteToFile(level: Int): Boolean =
        fileLoggingEnabled && level >= minLevelForFile

    fun d(tag: String, message: String) {
        if (shouldWriteToFile(Log.DEBUG)) {
            writeLog("DEBUG", tag, message, null)
        }
        Log.d(tag, message)
    }

    fun i(tag: String, message: String) {
        if (shouldWriteToFile(Log.INFO)) {
            writeLog("INFO", tag, message, null)
        }
        Log.i(tag, message)
    }

    fun w(tag: String, message: String) {
        if (shouldWriteToFile(Log.WARN)) {
            writeLog("WARN", tag, message, null)
        }
        Log.w(tag, message)
    }

    fun e(tag: String, message: String, throwable: Throwable? = null) {
        if (shouldWriteToFile(Log.ERROR)) {
            writeLog("ERROR", tag, message, throwable)
        }
        if (throwable != null) {
            Log.e(tag, message, throwable)
        } else {
            Log.e(tag, message)
        }
    }

    fun thread(tag: String, message: String) {
        val threadInfo = "线程: ${Thread.currentThread().name} [${Thread.currentThread().id}]"
        val fullMessage = "$message | $threadInfo"
        if (shouldWriteToFile(Log.DEBUG)) {
            writeLog("THREAD", tag, fullMessage, null)
        }
        Log.d(tag, fullMessage)
    }

    fun dataUpdate(tag: String, operation: String, data: String) {
        val threadInfo = Thread.currentThread().name
        val fullMessage = "[数据更新] $operation | $data | 线程: $threadInfo"
        if (shouldWriteToFile(Log.DEBUG)) {
            writeLog("DATA", tag, fullMessage, null)
        }
        Log.d(tag, fullMessage)
    }

    private fun writeLog(level: String, tag: String, message: String, throwable: Throwable?) {
        val dir = logDir ?: return
        if (!dir.exists()) {
            return
        }

        lock.withLock {
            try {
                val logFile = getTodayLogFile() ?: return@withLock
                val logEntry = buildLogEntry(level, tag, message, throwable)

                FileWriter(logFile, true).use { writer ->
                    writer.append(logEntry)
                    writer.flush()
                }
            } catch (e: IOException) {
                Log.e(TAG, "写入日志文件失败: ${e.message}", e)
            }
        }
    }

    private fun buildLogEntry(level: String, tag: String, message: String, throwable: Throwable?): String {
        val timestamp = dateFormat.format(Date())
        val threadInfo = Thread.currentThread().name
        val threadId = Thread.currentThread().id

        val builder = StringBuilder()
        builder.append("[$timestamp] ")
        builder.append("[$level] ")
        builder.append("[$tag] ")
        builder.append("[$threadInfo:$threadId] ")
        builder.append(message)
        builder.append("\n")

        if (throwable != null) {
            builder.append("异常堆栈:\n")
            throwable.stackTrace.forEach { element ->
                builder.append("  at ${element.className}.${element.methodName}(${element.fileName}:${element.lineNumber})\n")
            }
            builder.append("\n")
        }

        return builder.toString()
    }

    private fun getTodayLogFile(): File? {
        val dir = logDir ?: return null
        val fileName = "${LOG_FILE_PREFIX}${launchTimestamp}${LOG_FILE_SUFFIX}"
        return File(dir, fileName)
    }

    private fun cleanOldLogs() {
        val dir = logDir ?: return
        try {
            val files = dir.listFiles() ?: return
            val calendar = Calendar.getInstance()
            calendar.add(Calendar.DAY_OF_YEAR, -MAX_LOG_DAYS)
            val cutoffDate = fileDateFormat.format(calendar.time)

            files.forEach { file ->
                if (file.isFile && file.name.startsWith(LOG_FILE_PREFIX) && file.name.endsWith(LOG_FILE_SUFFIX)) {
                    val dateStr = file.name
                        .removePrefix(LOG_FILE_PREFIX)
                        .removeSuffix(LOG_FILE_SUFFIX)

                    if (dateStr < cutoffDate) {
                        val deleted = file.delete()
                        if (deleted) {
                            Log.d(TAG, "删除旧日志文件: ${file.name}")
                        }
                    }
                }
            }
        } catch (e: Exception) {
            Log.e(TAG, "清理旧日志失败: ${e.message}", e)
        }
    }

    fun getLogFiles(): List<File> {
        val dir = logDir ?: return emptyList()
        return dir.listFiles()
            ?.filter { it.isFile && it.name.startsWith(LOG_FILE_PREFIX) && it.name.endsWith(LOG_FILE_SUFFIX) }
            ?.sortedByDescending { it.lastModified() }
            ?: emptyList()
    }

    fun getTodayLogFilePath(): String? {
        return getTodayLogFile()?.absolutePath
    }
}
