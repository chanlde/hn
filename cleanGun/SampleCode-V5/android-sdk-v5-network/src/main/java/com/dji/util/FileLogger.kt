package com.dji.util

import android.content.Context
import android.os.Environment
import android.util.Log
import java.io.File
import java.io.FileWriter
import java.io.IOException
import java.text.SimpleDateFormat
import java.util.*
import java.util.concurrent.locks.ReentrantLock
import kotlin.concurrent.withLock

/**
 * 文件日志系统
 * 
 * 功能：
 * 1. 将日志写入外部存储的 Download 目录（方便随时访问）
 * 2. 记录线程信息、时间戳、日志级别
 * 3. 自动清理旧日志文件（保留最近7天）
 * 4. 线程安全
 * 
 * 日志位置：/storage/emulated/0/Download/AppLogs/
 */
object FileLogger {
    
    private const val TAG = "FileLogger"
    private const val LOG_DIR_NAME = "AppLogs"
    private const val LOG_FILE_PREFIX = "log_"
    private const val LOG_FILE_SUFFIX = ".txt"
    private const val MAX_LOG_DAYS = 7  // 保留最近7天的日志
    
    private var logDir: File? = null
    private val lock = ReentrantLock()
    private val dateFormat = SimpleDateFormat("yyyy-MM-dd HH:mm:ss.SSS", Locale.getDefault())
    private val fileDateFormat = SimpleDateFormat("yyyyMMdd", Locale.getDefault())
    // ...
    private val launchTimestamp = SimpleDateFormat("yyyyMMdd_HHmmss", Locale.getDefault()).format(Date())

    private var defaultUncaughtExceptionHandler: Thread.UncaughtExceptionHandler? = null
    private var isCrashHandlerRegistered = false
    
    /**
     * 全局异常处理器
     */
    private val uncaughtExceptionHandler = Thread.UncaughtExceptionHandler { thread, throwable ->
        // 记录崩溃信息到日志文件
        e("CRASH", "========================================", throwable)
        e("CRASH", "未捕获的异常导致应用崩溃", throwable)
        e("CRASH", "崩溃线程: ${thread.name} [${thread.id}]", throwable)
        e("CRASH", "异常类型: ${throwable.javaClass.name}", throwable)
        e("CRASH", "异常消息: ${throwable.message}", throwable)
        e("CRASH", "========================================", throwable)
        
        // 确保日志写入磁盘
        flushLogs()
        
        // 调用系统默认异常处理器（会显示崩溃对话框）
        defaultUncaughtExceptionHandler?.uncaughtException(thread, throwable)
    }
    
    /**
     * 初始化日志系统
     * @param context 上下文
     * @param enableCrashHandler 是否启用全局异常捕获（默认 true）
     */
    fun init(context: Context, enableCrashHandler: Boolean = true) {
        try {
            // 使用外部存储的 Download 目录，方便随时访问
            val downloadDir = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS)
            logDir = File(downloadDir, LOG_DIR_NAME)
            
            if (!logDir!!.exists()) {
                val created = logDir!!.mkdirs()
                if (!created) {
                    Log.e(TAG, "创建日志目录失败: ${logDir!!.absolutePath}")
                    // 备用方案：使用应用外部文件目录
                    val fallbackDir = context.getExternalFilesDir(null)
                    logDir = File(fallbackDir, LOG_DIR_NAME)
                    logDir!!.mkdirs()
                }
            }
            
            // 清理旧日志
            cleanOldLogs()
            
            Log.d(TAG, "文件日志系统初始化完成")
            Log.d(TAG, "日志目录: ${logDir!!.absolutePath}")
            d("FileLogger", "文件日志系统初始化完成，日志目录: ${logDir!!.absolutePath}")
            
            // 注册全局异常处理器
            if (enableCrashHandler && !isCrashHandlerRegistered) {
                registerCrashHandler()
            }
        } catch (e: Exception) {
            Log.e(TAG, "初始化文件日志系统失败: ${e.message}", e)
        }
    }
    
    /**
     * 注册全局异常处理器
     */
    private fun registerCrashHandler() {
        try {
            defaultUncaughtExceptionHandler = Thread.getDefaultUncaughtExceptionHandler()
            Thread.setDefaultUncaughtExceptionHandler(uncaughtExceptionHandler)
            isCrashHandlerRegistered = true
            Log.d(TAG, "全局异常处理器已注册")
            d("FileLogger", "全局异常处理器已注册，崩溃信息将自动记录到日志文件")
        } catch (e: Exception) {
            Log.e(TAG, "注册全局异常处理器失败: ${e.message}", e)
        }
    }
    
    /**
     * 取消注册全局异常处理器
     */
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
    
    /**
     * 强制刷新日志到磁盘
     */
    private fun flushLogs() {
        lock.withLock {
            // 这里可以添加额外的刷新逻辑
            // 由于 FileWriter 使用了 use{} 和 flush()，基本已经是实时写入的
        }
    }
    
    /**
     * 获取日志目录
     */
    fun getLogDir(): File? = logDir
    
    /**
     * Debug 级别日志
     */
    fun d(tag: String, message: String) {
        writeLog("DEBUG", tag, message, null)
        Log.d(tag, message)
    }
    
    /**
     * Info 级别日志
     */
    fun i(tag: String, message: String) {
        writeLog("INFO", tag, message, null)
        Log.i(tag, message)
    }
    
    /**
     * Warning 级别日志
     */
    fun w(tag: String, message: String) {
        writeLog("WARN", tag, message, null)
        Log.w(tag, message)
    }
    
    /**
     * Error 级别日志
     */
    fun e(tag: String, message: String, throwable: Throwable? = null) {
        writeLog("ERROR", tag, message, throwable)
        if (throwable != null) {
            Log.e(tag, message, throwable)
        } else {
            Log.e(tag, message)
        }
    }
    
    /**
     * 记录线程信息
     */
    fun thread(tag: String, message: String) {
        val threadInfo = "线程: ${Thread.currentThread().name} [${Thread.currentThread().id}]"
        val fullMessage = "$message | $threadInfo"
        writeLog("THREAD", tag, fullMessage, null)
        Log.d(tag, fullMessage)
    }
    
    /**
     * 记录数据更新操作（用于调试线程安全）
     */
    fun dataUpdate(tag: String, operation: String, data: String) {
        val threadInfo = Thread.currentThread().name
        val fullMessage = "[数据更新] $operation | $data | 线程: $threadInfo"
        writeLog("DATA", tag, fullMessage, null)
        Log.d(tag, fullMessage)
    }
    
    /**
     * 写入日志文件
     */
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
    
    /**
     * 构建日志条目
     */
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
    
    /**
     * 获取今天的日志文件
     */
    private fun getTodayLogFile(): File? {
        val dir = logDir ?: return null
        val fileName = "${LOG_FILE_PREFIX}${launchTimestamp}${LOG_FILE_SUFFIX}"
        return File(dir, fileName)
    }
    
    /**
     * 清理旧日志文件（保留最近N天）
     */
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
    
    /**
     * 获取日志文件列表
     */
    fun getLogFiles(): List<File> {
        val dir = logDir ?: return emptyList()
        return dir.listFiles()
            ?.filter { it.isFile && it.name.startsWith(LOG_FILE_PREFIX) && it.name.endsWith(LOG_FILE_SUFFIX) }
            ?.sortedByDescending { it.lastModified() }
            ?: emptyList()
    }
    
    /**
     * 获取今天的日志文件路径
     */
    fun getTodayLogFilePath(): String? {
        return getTodayLogFile()?.absolutePath
    }
}

