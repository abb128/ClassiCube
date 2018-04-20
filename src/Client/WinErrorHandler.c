#include "ErrorHandler.h"
#include "Platform.h"
#define WIN32_LEAN_AND_MEAN
#define NOSERVICE
#define NOMCX
#define NOIME
#include <Windows.h>

/* TODO: These might be better off as a function. */
#define ErrorHandler_WriteLogBody(raw_msg)\
UInt8 logMsgBuffer[String_BufferSize(2047)];\
String logMsg = String_InitAndClearArray(logMsgBuffer);\
String_AppendConst(&logMsg, "ClassicalSharp crashed.\r\n");\
String_AppendConst(&logMsg, "Message: ");\
String_AppendConst(&logMsg, raw_msg);\
String_AppendConst(&logMsg, "\r\n");

#define ErrorHandler_WriteLogEnd()\
String_AppendConst(&logMsg, "\r\nPlease report the crash to github.com/UnknownShadow200/ClassicalSharp/issues so we can fix it.");


void ErrorHandler_Init(const UInt8* logFile) {
	/* TODO: Open log file */
}

void ErrorHandler_Log(STRING_PURE String* msg) {
	/* TODO: write to log file */
}

void ErrorHandler_Fail(const UInt8* raw_msg) {
	/* TODO: write to log file */
	ErrorHandler_WriteLogBody(raw_msg);
	ErrorHandler_WriteLogEnd();

	ErrorHandler_ShowDialog("We're sorry", logMsg.buffer);
	ExitProcess(1);
}

void ErrorHandler_FailWithCode(ReturnCode code, const UInt8* raw_msg) {
	/* TODO: write to log file */
	ErrorHandler_WriteLogBody(raw_msg);
	String_AppendConst(&logMsg, "Return code: ");
	String_AppendInt32(&logMsg, (Int32)code);
	String_AppendConst(&logMsg, "\r\n");
	ErrorHandler_WriteLogEnd();

	ErrorHandler_ShowDialog("We're sorry", logMsg.buffer);
	ExitProcess(code);
}

void ErrorHandler_ShowDialog(const UInt8* title, const UInt8* msg) {
	HWND win = GetActiveWindow(); /* TODO: It's probably wrong to use GetActiveWindow() here */
	MessageBoxA(win, msg, title, 0);
}