#pragma once

// функции вынесены из SDK чтобы была возможность собрать приложение с поддержкой формата WinXP
// fltUser.h недоступна при сборке с поддержкой XP (ошибка путей/установки SDK/DDK?)

#include <Windows.h>
extern "C" HRESULT
WINAPI
FilterConnectCommunicationPort(
	_In_ LPCWSTR lpPortName,
	_In_ DWORD dwOptions,
	_In_reads_bytes_opt_(wSizeOfContext) LPCVOID lpContext,
	_In_ WORD wSizeOfContext,
	_In_opt_ LPSECURITY_ATTRIBUTES lpSecurityAttributes,
	_Outptr_ HANDLE *hPort
);

extern "C" HRESULT
WINAPI
FilterSendMessage(
	_In_ HANDLE hPort,
	_In_reads_bytes_(dwInBufferSize) LPVOID lpInBuffer,
	_In_ DWORD dwInBufferSize,
	_Out_writes_bytes_to_opt_(dwOutBufferSize, *lpBytesReturned) LPVOID lpOutBuffer,
	_In_ DWORD dwOutBufferSize,
	_Out_ LPDWORD lpBytesReturned
);

