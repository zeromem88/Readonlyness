
#include "Helper.h"
#include <ntddk.h>

/// SID'ы системных пользователей
static SID LocalSystemSID[12] = { SID_REVISION, 0x01, SECURITY_NT_AUTHORITY, SECURITY_LOCAL_SYSTEM_RID };
static SID LocalServiceSID[12] = { SID_REVISION, 0x01, SECURITY_NT_AUTHORITY, SECURITY_LOCAL_SERVICE_RID };
static SID NetworkSystemSID[12] = { SID_REVISION, 0x01, SECURITY_NT_AUTHORITY, SECURITY_NETWORK_SERVICE_RID };
// SID группы "Сеть", к которой относятся все пользователи авторизованные через сетевое подключение
static SID NetworkSID[12] = { SID_REVISION, 0x01, SECURITY_NT_AUTHORITY, SECURITY_NETWORK_RID };
// SID группы локальных администраторов
static SID AdminsSID[16] = { SID_REVISION, 0x02, SECURITY_NT_AUTHORITY, SECURITY_BUILTIN_DOMAIN_RID, 0x20, 0x02, 0x0, 0x0 };
// SID пользователя, под которым работает LogonUI.exe
static SID WindowManagerSID[16] = { SID_REVISION, 0x02, SECURITY_NT_AUTHORITY, SECURITY_WINDOW_MANAGER_BASE_RID, 0x1L };

extern PFLT_FILTER pFilterHandle;

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, GetObjectName)
#pragma alloc_text(PAGE, IsROAccessType)
#pragma alloc_text(PAGE, SetROAccess)
#endif // ALLOC_PRAGMA

/// Функция получения полного имени объекта
NTSTATUS GetObjectName(IN PVOID Object, OUT PUNICODE_STRING *returnValue)
{
	PAGED_CODE();

	NTSTATUS                  ntStatus = STATUS_UNSUCCESSFUL;
	POBJECT_NAME_INFORMATION  nameInfo = NULL;
	ULONG                     nameInfoSize = 0;


	// Проверка входных параметров
	if ((NULL == Object) || (NULL == returnValue))
		return STATUS_INVALID_PARAMETER;


	// Запрос размера буфера
	ntStatus = ObQueryNameString(Object, nameInfo, 0, &nameInfoSize);

	// Если передан слишком маленький буфер
	if (STATUS_INFO_LENGTH_MISMATCH == ntStatus)
	{
		// Выделение памяти
		nameInfo = (POBJECT_NAME_INFORMATION)ExAllocatePoolWithTag(NonPagedPool, nameInfoSize, MEM_TAG_OBJECT_NAME);
		if (!nameInfo)
			return STATUS_INSUFFICIENT_RESOURCES;


		// Запрос полного имени объекта
		ntStatus = ObQueryNameString(Object, nameInfo, nameInfoSize, &nameInfoSize);

		if (NT_SUCCESS(ntStatus))
		{
			// Выделение памяти
			*returnValue = (PUNICODE_STRING)ExAllocatePoolWithTag(NonPagedPool, sizeof(UNICODE_STRING), MEM_TAG_OBJECT_NAME);
			if (*returnValue)
			{
				// Задание максимального размера строки
				(*returnValue)->MaximumLength = nameInfo->Name.MaximumLength;

				// Выделение памяти
				(*returnValue)->Buffer = (PWCH)ExAllocatePoolWithTag(NonPagedPool, (*returnValue)->MaximumLength + sizeof(UNICODE_NULL), MEM_TAG_OBJECT_NAME);
				if ((*returnValue)->Buffer)
				{
					// Копирование строки
					RtlCopyUnicodeString(*returnValue, &nameInfo->Name);

				}
				else
				{
					// Освобождение памяти
					ExFreePoolWithTag(*returnValue, MEM_TAG_OBJECT_NAME);
					*returnValue = NULL;

					// Установка кода ошибки
					ntStatus = STATUS_INSUFFICIENT_RESOURCES;

				}

			}
			else
			{
				// Установка кода ошибки
				ntStatus = STATUS_INSUFFICIENT_RESOURCES;

			}

		}

		// Освобождение памяти
		ExFreePoolWithTag(nameInfo, MEM_TAG_OBJECT_NAME);

	}

	return ntStatus;
}

BOOLEAN CheckIfWinXP()
{
	RTL_OSVERSIONINFOEXW VerInfo = { 0 };
	ULONGLONG CondMask = 0;

	// XP, SRV2003
	VerInfo.dwOSVersionInfoSize = sizeof(VerInfo);
	VerInfo.dwMajorVersion = 6;
	VerInfo.dwMinorVersion = 0;
	// VerInfo.dwMajorVersion < 6
	VER_SET_CONDITION(CondMask, VER_MAJORVERSION, VER_LESS);
	// VerInfo.dwMinorVersion < 0
	VER_SET_CONDITION(CondMask, VER_MINORVERSION, VER_LESS);


	NTSTATUS status = RtlVerifyVersionInfo(&VerInfo, VER_MAJORVERSION | VER_MINORVERSION, CondMask);

	if (NT_SUCCESS(status))
	{
		return TRUE;
	}
	else
	{
		return FALSE;
	}
}

// Функция получения имени устройства диска
NTSTATUS GetDosDeviceName(WCHAR DiskLetter, PUNICODE_STRING *returnValue)
{
	NTSTATUS ntStatus = STATUS_UNSUCCESSFUL;
	HANDLE FileHandle = NULL;
	OBJECT_ATTRIBUTES FileAttributes = { 0 };
	UNICODE_STRING FileName = { 0, 0, NULL };
	IO_STATUS_BLOCK ioStatusBlock = { 0 };
	PVOID FileObject = NULL;
	PWCH strTemplate = NULL;
	// шаблон не работает под XP через открытие тома и запрос имени по ObQueryNameString
	PWCH strTemplateVistaAbove = L"\\DosDevices\\_:";
	USHORT DiskLetterIndex = 0;
	// размеры шаблонов в символах с учетом NULL-символа
	USHORT strTemplateSize = 0;

	strTemplate = strTemplateVistaAbove;
	strTemplateSize = 15; 
	DiskLetterIndex = 12;

	// Проверка входных параметров
	if (returnValue == NULL)
	{
		return STATUS_INVALID_PARAMETER;
	}

	// Инициализация имени символической ссылки
	FileName.Buffer = ExAllocatePoolWithTag(NonPagedPool, strTemplateSize * sizeof(UNICODE_NULL), MEM_TAG_OBJECT_NAME);
	if (FileName.Buffer == NULL)
	{
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	RtlCopyMemory(FileName.Buffer, strTemplate, strTemplateSize * sizeof(UNICODE_NULL));

	// Меняем _ на букву диска
	FileName.Buffer[DiskLetterIndex] = DiskLetter;

	// устанавливаем размеры
	FileName.Length = (strTemplateSize - 1) * sizeof(UNICODE_NULL);
	FileName.MaximumLength = strTemplateSize * sizeof(UNICODE_NULL);

	if (CheckIfWinXP())
	{
		PFLT_VOLUME pVolume = NULL;
		ntStatus = FltGetVolumeFromName(pFilterHandle, &FileName, &pVolume);

		if (NT_SUCCESS(ntStatus))
		{
			ULONG bufSize = 0;

			ntStatus = FltGetVolumeName(pVolume, NULL, &bufSize);

			if (STATUS_BUFFER_TOO_SMALL == ntStatus)
			{
				*returnValue = (PUNICODE_STRING)ExAllocatePoolWithTag(NonPagedPool, sizeof(UNICODE_STRING), MEM_TAG_OBJECT_NAME);
				if (*returnValue)
				{
					(*returnValue)->Length = 0;
					(*returnValue)->MaximumLength = (USHORT) bufSize;
					(*returnValue)->Buffer = ExAllocatePoolWithTag(NonPagedPool, bufSize, MEM_TAG_OBJECT_NAME);
					
					if ((*returnValue)->Buffer != NULL)
					{
						FltGetVolumeName(pVolume, *returnValue, NULL);
						ntStatus = STATUS_SUCCESS;
					}
					else
					{
						ExFreePoolWithTag(*returnValue, MEM_TAG_OBJECT_NAME);
					}
				}
			}

			FltObjectDereference(pVolume);
		}
	}
	else
	{
		InitializeObjectAttributes(&FileAttributes, &FileName, OBJ_CASE_INSENSITIVE, NULL, NULL);

		// Открытие файла
		ntStatus = ZwCreateFile(&FileHandle, FILE_READ_ATTRIBUTES | SYNCHRONIZE, &FileAttributes,
			&ioStatusBlock, NULL, FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ, FILE_OPEN,
			FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);

		if (NT_SUCCESS(ntStatus))
		{
			// Получение указателя на объект
			ntStatus = ObReferenceObjectByHandle(FileHandle, READ_CONTROL, NULL, KernelMode, &FileObject, NULL);
			if (NT_SUCCESS(ntStatus))
			{
				// Получение имени файла
				ntStatus = GetObjectName(FileObject, returnValue);

				// Удаление указателя на объект
				ObDereferenceObject(FileObject);
			}

			// Закрытие файла
			ZwClose(FileHandle);
		}
	}

	ExFreePoolWithTag(FileName.Buffer, MEM_TAG_OBJECT_NAME);

	return ntStatus;
}

// Функция информации о токене (системный пользователь? сетевой пользователь? uid?)
NTSTATUS GetTokenInfo(BOOLEAN *IsAdmin, BOOLEAN *IsSystemUser, BOOLEAN *IsNetUser, PSECURITY_SUBJECT_CONTEXT SubSecContext)
{
	NTSTATUS ntStatus = STATUS_UNSUCCESSFUL;
	PACCESS_TOKEN pToken = NULL;
	PTOKEN_USER pTokenUser = NULL;
	PTOKEN_GROUPS pTokenGroups = NULL;


	// Инициализация результата
	*IsAdmin = FALSE;
	*IsSystemUser = FALSE;
	*IsNetUser = FALSE;


	// Если передан контекст безопасности
	if (SubSecContext)
	{
		// Получение токена на основе переданного контекста безопасности
		pToken = SeQuerySubjectContextToken(SubSecContext);
	}
	else
	{
		SECURITY_SUBJECT_CONTEXT SecContext;

		// Получение текущего контекста безопасности
		SeCaptureSubjectContext(&SecContext);
		// Получение токена
		pToken = SeQuerySubjectContextToken(&SecContext);
	}


	// Получение пользователя токена
	ntStatus = SeQueryInformationToken(pToken, TokenUser, (PVOID *)&pTokenUser);

	if (NT_SUCCESS(ntStatus) && RtlValidSid(pTokenUser->User.Sid))
	{
		PSID sid = pTokenUser->User.Sid;

		// Если это системный пользователь
		if (RtlEqualSid((PSID)LocalSystemSID, sid) ||
			RtlEqualSid((PSID)LocalServiceSID, sid) ||
			RtlEqualSid((PSID)NetworkSystemSID, sid) ||
			RtlEqualSid((PSID)WindowManagerSID, sid))
		{
			// Установка флага
			*IsSystemUser = TRUE;
		}

		// Освобождение памяти
		ExFreePool(pTokenUser);
	}


	// Получение групп пользователя токена
	ntStatus = SeQueryInformationToken(pToken, TokenGroups, (PVOID *)&pTokenGroups);

	if (NT_SUCCESS(ntStatus))
	{
		ULONG i = 0;
		ULONG break_cond = 2;

		for (i = 0; i < pTokenGroups->GroupCount; ++i)
		{
			SID_AND_ATTRIBUTES sid_and_attr = pTokenGroups->Groups[i];

			if (RtlEqualSid(sid_and_attr.Sid, NetworkSID))
			{
				*IsNetUser = TRUE;
				break_cond++;
			}

			if (RtlEqualSid(sid_and_attr.Sid, AdminsSID))
			{
				*IsAdmin = TRUE;
				break_cond++;
			}

			if (break_cond >= 2)
			{
				break;
			}
		}

		// Освобождение памяти
		ExFreePool(pTokenGroups);
	}


	return ntStatus;
}


// Функция получения имени тома
NTSTATUS GetVolumeName(const PFLT_VOLUME Volume, PUNICODE_STRING VolumeName)
{
	NTSTATUS ntStatus = STATUS_UNSUCCESSFUL;
	ULONG    VolumeNameSize = 0;
	BOOLEAN  fAllocate = FALSE;


	// Проверка входных параметров
	if ((NULL == Volume) || (NULL == VolumeName))
	{
		return STATUS_INVALID_PARAMETER;
	}

	// Если передана пустая строка
	if (NULL == VolumeName->Buffer)
	{
		// Инициализация имени тома
		VolumeName->Buffer = NULL;
		VolumeName->Length = 0;
		VolumeName->MaximumLength = 0;

		// Получение размера буфера под имя тома
		ntStatus = FltGetVolumeName(Volume, VolumeName, &VolumeNameSize);
		if (STATUS_BUFFER_TOO_SMALL == ntStatus)
		{
			// Инициализация буфера
			VolumeName->Length = (USHORT)VolumeNameSize + sizeof(UNICODE_NULL);
			VolumeName->MaximumLength = VolumeName->Length;
			VolumeName->Buffer = (PWCH)ExAllocatePoolWithTag(NonPagedPool, VolumeName->MaximumLength, MEM_TAG_VOLUME_NAME);

			// Если ошибка выделения памяти
			if (!VolumeName->Buffer)
				return STATUS_INSUFFICIENT_RESOURCES;

			// Установка флага
			fAllocate = TRUE;
		}
	}
	else
	{
		// Если буфер слишком мал
		if (VolumeName->Length < sizeof(UNICODE_NULL))
			return STATUS_BUFFER_TOO_SMALL;

		// Вычисление размера буфера без учета места под NULL-символ
		VolumeNameSize = VolumeName->Length - sizeof(UNICODE_NULL);
	}


	// Получение имени тома
	ntStatus = FltGetVolumeName(Volume, VolumeName, &VolumeNameSize);

	// Если ошибка и осуществлялось выделение памяти
	if (!NT_SUCCESS(ntStatus) && fAllocate)
	{
		// Освобождение памяти
		ExFreePoolWithTag(VolumeName->Buffer, MEM_TAG_VOLUME_NAME);
	}

	return ntStatus;
}
// Функция определения запроса только для чтения
BOOLEAN IsROAccessType(const PFLT_IO_PARAMETER_BLOCK Iopb)
{
	PAGED_CODE();

	// Проверка входных параметров
	if (NULL == Iopb)
	{
		return FALSE;
	}

	if (IRP_MJ_CREATE != Iopb->MajorFunction)
	{
		return FALSE;
	}

	if (((Iopb->Parameters.Create.Options >> 24) & 7) != FILE_OPEN)
	{
		return FALSE;
	}

	if (FlagOn(Iopb->Parameters.Create.SecurityContext->DesiredAccess,
		(FILE_DELETE_CHILD | WRITE_DAC | WRITE_OWNER | GENERIC_WRITE | FILE_WRITE_DATA |
			FILE_WRITE_ATTRIBUTES | FILE_WRITE_EA | FILE_APPEND_DATA | DELETE)))
	{
		return FALSE;
	}

	if (FlagOn(Iopb->Parameters.Create.SecurityContext->FullCreateOptions,
		(FILE_DELETE_ON_CLOSE | FILE_WRITE_THROUGH)))
	{
		return FALSE;
	}

	return TRUE;
}


// Функция принудительной установки режима доступа только для чтения
BOOLEAN SetROAccess(const PFLT_IO_PARAMETER_BLOCK Iopb)
{
	PAGED_CODE();

	// Проверка входных параметров
	if (NULL == Iopb)
		return FALSE;

	if (Iopb->MajorFunction != IRP_MJ_CREATE)
		return FALSE;

	// Если это не операция открытия
	if ((((Iopb->Parameters.Create.Options >> 24) & 7) != FILE_OPEN) &&
		(((Iopb->Parameters.Create.Options >> 24) & 7) != FILE_OPEN_IF))
	{
		return FALSE;
	}

	// Если запрошено не только открытие с созданием при несуществовании,
	// сброс флага создания
	if ((((Iopb->Parameters.Create.Options >> 24) & 7) == FILE_OPEN_IF))
		Iopb->Parameters.Create.Options =
		(Iopb->Parameters.Create.Options & ~(7 << 24)) | (FILE_OPEN << 24);

	// Удаление флагов записи
	if (FlagOn(Iopb->Parameters.Create.SecurityContext->FullCreateOptions,
		(FILE_DELETE_ON_CLOSE | FILE_WRITE_THROUGH)))
		ClearFlag(Iopb->Parameters.Create.SecurityContext->FullCreateOptions,
		(FILE_DELETE_ON_CLOSE | FILE_WRITE_THROUGH));

	// Если запрошено чтение (возможно и запись)
	if (FlagOn(Iopb->Parameters.Create.SecurityContext->DesiredAccess,
		(READ_CONTROL | GENERIC_READ | FILE_READ_DATA |
			FILE_READ_ATTRIBUTES | FILE_READ_EA | FILE_EXECUTE)))
	{
		// Удаление флагов записи
		ClearFlag(Iopb->Parameters.Create.SecurityContext->DesiredAccess,
			(FILE_DELETE_CHILD | WRITE_DAC | WRITE_OWNER | GENERIC_WRITE | FILE_WRITE_DATA |
				FILE_WRITE_ATTRIBUTES | FILE_WRITE_EA | FILE_APPEND_DATA | DELETE));
		ClearFlag(Iopb->Parameters.Create.SecurityContext->AccessState->RemainingDesiredAccess,
			(FILE_DELETE_CHILD | WRITE_DAC | WRITE_OWNER | GENERIC_WRITE | FILE_WRITE_DATA |
				FILE_WRITE_ATTRIBUTES | FILE_WRITE_EA | FILE_APPEND_DATA | DELETE));
	}
	else
	{
		return FALSE;
	}

	return TRUE;
}

