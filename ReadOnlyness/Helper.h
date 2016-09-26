
#pragma once

#include "CommonKernel.h"

#define SeQuerySubjectContextToken( SubjectContext ) \
        ( ARGUMENT_PRESENT( ((PSECURITY_SUBJECT_CONTEXT) SubjectContext)->ClientToken) ? \
            ((PSECURITY_SUBJECT_CONTEXT) SubjectContext)->ClientToken : \
            ((PSECURITY_SUBJECT_CONTEXT) SubjectContext)->PrimaryToken )


typedef NTSTATUS(*QUERY_INFO_PROCESS) (IN HANDLE ProcessHandle,
	IN PROCESSINFOCLASS ProcessInformationClass,
	__out_bcount(ProcessInformationLength) PVOID ProcessInformation,
	IN ULONG ProcessInformationLength,
	__out_opt PULONG ReturnLength);


// Функция получения полного имени объекта
NTSTATUS GetObjectName(IN PVOID Object, OUT PUNICODE_STRING *returnValue);

// Функция получения имени устройства диска
NTSTATUS GetDosDeviceName(WCHAR DiskLetter, PUNICODE_STRING *returnValue);
// NTSTATUS GetSystemDeviceNameByFltMgr(PFLT_FILTER pFilter, PUNICODE_STRING *returnValue);

// Функция информации о токене (админ?, системный пользователь?, сетевой пользователь?)
NTSTATUS GetTokenInfo(BOOLEAN *IsAdmin, BOOLEAN *IsSystemUser, BOOLEAN *IsNetUser, PSECURITY_SUBJECT_CONTEXT SubSecContext);

// Функция получения имени тома
NTSTATUS GetVolumeName(const PFLT_VOLUME Volume, PUNICODE_STRING VolumeName);

// Функция определения запроса только для чтения
BOOLEAN IsROAccessType(const PFLT_IO_PARAMETER_BLOCK Iopb);

// Функция принудительной установки режима доступа только для чтения
BOOLEAN SetROAccess(const PFLT_IO_PARAMETER_BLOCK Iopb);

// Проверка факта запуска под XP
BOOLEAN CheckIfWinXP();