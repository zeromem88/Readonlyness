
#include "CommonKernel.h"
#include "StringFilters.h"
#include "Helper.h"

#pragma prefast(disable:__WARNING_ENCODE_MEMBER_FUNCTION_POINTER, "Not valid for kernel mode drivers")

PFLT_FILTER pFilterHandle;
ULONG_PTR OperationStatusCtx = 1;

ULONG gTraceFlags = 0;

PFLT_PORT pServerPort;
PFLT_PORT pClientPort;

EXTERN_C_START

DRIVER_INITIALIZE DriverEntry;
NTSTATUS
DriverEntry(
	_In_ PDRIVER_OBJECT DriverObject,
	_In_ PUNICODE_STRING RegistryPath
);

NTSTATUS
ReadOnlynessInstanceSetup(
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_In_ FLT_INSTANCE_SETUP_FLAGS Flags,
	_In_ DEVICE_TYPE VolumeDeviceType,
	_In_ FLT_FILESYSTEM_TYPE VolumeFilesystemType
);

VOID
ReadOnlynessInstanceTeardownStart(
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_In_ FLT_INSTANCE_TEARDOWN_FLAGS Flags
);

VOID
ReadOnlynessInstanceTeardownComplete(
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_In_ FLT_INSTANCE_TEARDOWN_FLAGS Flags
);

NTSTATUS
ReadOnlynessUnload(
	_In_ FLT_FILTER_UNLOAD_FLAGS Flags
);

NTSTATUS
ReadOnlynessInstanceQueryTeardown(
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_In_ FLT_INSTANCE_QUERY_TEARDOWN_FLAGS Flags
);

FLT_PREOP_CALLBACK_STATUS
ReadOnlynessPreOperation(
	_Inout_ PFLT_CALLBACK_DATA Data,
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_Flt_CompletionContext_Outptr_ PVOID *CompletionContext
);

FLT_POSTOP_CALLBACK_STATUS
ReadOnlynessPostOperation(
	_Inout_ PFLT_CALLBACK_DATA Data,
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_In_opt_ PVOID CompletionContext,
	_In_ FLT_POST_OPERATION_FLAGS Flags
);

NTSTATUS ConnectCallback(
	__in PFLT_PORT ClientPort,
	__in PVOID ServerPortCookie,
	__in_bcount(SizeOfContext) PVOID ConnectionContext,
	__in ULONG SizeOfContext,
	__deref_out_opt PVOID *ConnectionCookie
);

VOID DisconnectCallback(__in_opt PVOID ConnectionCookie);

NTSTATUS MessageCallback(
	__in PVOID ConnectionCookie,
	__in_bcount_opt(InputBufferSize) PVOID InputBuffer,
	__in ULONG InputBufferSize,
	__out_bcount_part_opt(OutputBufferSize, *ReturnOutputBufferLength) PVOID OutputBuffer,
	__in ULONG OutputBufferSize,
	__out PULONG ReturnOutputBufferLength
);


EXTERN_C_END

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, ReadOnlynessUnload)
#pragma alloc_text(PAGE, ReadOnlynessInstanceQueryTeardown)
#pragma alloc_text(PAGE, ReadOnlynessInstanceSetup)
#pragma alloc_text(PAGE, ReadOnlynessInstanceTeardownStart)
#pragma alloc_text(PAGE, ReadOnlynessInstanceTeardownComplete)
#pragma alloc_text(PAGE, ConnectCallback)
#pragma alloc_text(PAGE, DisconnectCallback)
#pragma alloc_text(PAGE, MessageCallback)
#endif

//
//  operation registration
//

CONST FLT_OPERATION_REGISTRATION Callbacks[] = {
	{
		IRP_MJ_CREATE,
		0,
		ReadOnlynessPreOperation,
		ReadOnlynessPostOperation
	},

	{
		IRP_MJ_OPERATION_END
	}
};

//
//  This defines what we want to filter with FltMgr
//

CONST FLT_REGISTRATION FilterRegistration = 
{

	sizeof(FLT_REGISTRATION),				//  Size
	FLT_REGISTRATION_VERSION,				//  Version
	0,										//  Flags

	NULL,									//  Context
	Callbacks,								//  Operation callbacks

	ReadOnlynessUnload,						//  MiniFilterUnload

	ReadOnlynessInstanceSetup,				//  InstanceSetup
	ReadOnlynessInstanceQueryTeardown,		//  InstanceQueryTeardown
	ReadOnlynessInstanceTeardownStart,		//  InstanceTeardownStart
	ReadOnlynessInstanceTeardownComplete,	//  InstanceTeardownComplete

	NULL,									//  GenerateFileName
	NULL,									//  GenerateDestinationFileName
	NULL									//  NormalizeNameComponent

};



NTSTATUS
ReadOnlynessInstanceSetup(
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_In_ FLT_INSTANCE_SETUP_FLAGS Flags,
	_In_ DEVICE_TYPE VolumeDeviceType,
	_In_ FLT_FILESYSTEM_TYPE VolumeFilesystemType
)
{
	UNREFERENCED_PARAMETER(FltObjects);
	UNREFERENCED_PARAMETER(Flags);
	UNREFERENCED_PARAMETER(VolumeDeviceType);
	UNREFERENCED_PARAMETER(VolumeFilesystemType);

	PAGED_CODE();

	PT_DBG_PRINT(PTDBG_TRACE_ROUTINES,
		("ReadOnlyness!ReadOnlynessInstanceSetup: Entered\n"));

	return STATUS_SUCCESS;
}



/*************************************************************************
	MiniFilter initialization and unload routines.
*************************************************************************/

NTSTATUS
DriverEntry(
	_In_ PDRIVER_OBJECT DriverObject,
	_In_ PUNICODE_STRING RegistryPath
)
{
	UNREFERENCED_PARAMETER(RegistryPath);

	PSECURITY_DESCRIPTOR pSecurityDescriptor = NULL;
	NTSTATUS status;
	UNICODE_STRING roPortName;
	OBJECT_ATTRIBUTES objectAttributes = { 0 };

	PT_DBG_PRINT(PTDBG_TRACE_ROUTINES,
		("ReadOnlyness!DriverEntry: Entered\n"));

	status = FltRegisterFilter(DriverObject,
		&FilterRegistration,
		&pFilterHandle);

	if (!NT_SUCCESS(status))
	{
		PT_DBG_PRINT(PTDBG_TRACE_ROUTINES, ("ReadOnlyness!DriverEntry: ERROR FltRegisterFilter - %08x\n", status));
		return status;
	}

	status = FltBuildDefaultSecurityDescriptor(&pSecurityDescriptor, FLT_PORT_ALL_ACCESS);

	if (!NT_SUCCESS(status))
	{
		PT_DBG_PRINT(PTDBG_TRACE_ROUTINES, ("ReadOnlyness!DriverEntry: ERROR FltBuildDefaultSecurityDescriptor - %08x\n", status));
		return status;
	}

	RtlInitUnicodeString(&roPortName, READONLYNESS_PORT_NAME);
	InitializeObjectAttributes(&objectAttributes, &roPortName,	OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL,	pSecurityDescriptor);

	status = FltCreateCommunicationPort(pFilterHandle, &pServerPort, &objectAttributes,	NULL, ConnectCallback, DisconnectCallback, MessageCallback,	1);

	FltFreeSecurityDescriptor(pSecurityDescriptor);

	if (!NT_SUCCESS(status))
	{
		PT_DBG_PRINT(PTDBG_TRACE_ROUTINES, ("ReadOnlyness!DriverEntry: ERROR FltCreateCommunicationPort - %08x\n", status));
		return status;
	}
	

	if (NT_SUCCESS(status)) 
	{
		// Иницлиализируем списки фильтров
		InitStringFilters();
		
		status = FltStartFiltering(pFilterHandle);

		if (!NT_SUCCESS(status)) 
		{
			FltUnregisterFilter(pFilterHandle);
		}
	}

	return status;
}

NTSTATUS
ReadOnlynessUnload(
	_In_ FLT_FILTER_UNLOAD_FLAGS Flags
)
{
	UNREFERENCED_PARAMETER(Flags);

	PAGED_CODE();

	PT_DBG_PRINT(PTDBG_TRACE_ROUTINES,
		("ReadOnlyness!ReadOnlynessUnload: Entered\n"));

	FltCloseCommunicationPort(pServerPort);

	FltUnregisterFilter(pFilterHandle);

	DeinitStringFilters();

	return STATUS_SUCCESS;
}


FLT_PREOP_CALLBACK_STATUS
ReadOnlynessPreOperation(
	_Inout_ PFLT_CALLBACK_DATA Data,
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_Flt_CompletionContext_Outptr_ PVOID *CompletionContext
)
{
	UNREFERENCED_PARAMETER(CompletionContext);

	PT_DBG_PRINT(PTDBG_TRACE_ROUTINES,
		("ReadOnlyness!ReadOnlynessPreOperation: Entered\n"));

	NTSTATUS status = FLT_PREOP_SUCCESS_NO_CALLBACK;
	PFLT_IO_PARAMETER_BLOCK ParameterBlock = Data->Iopb;
	PFILE_OBJECT FileObject = FltObjects->FileObject;
	PFLT_FILE_NAME_INFORMATION pFileNameInformation = NULL;
	BOOLEAN IsAdmin = FALSE, IsSystem = FALSE, IsNetwork = FALSE;

	if (ParameterBlock->MajorFunction == IRP_MJ_CREATE)
	{
		// Проверка на системного пользователя
		status = GetTokenInfo(&IsAdmin,	&IsSystem, &IsNetwork, &(Data->Iopb->Parameters.Create.SecurityContext->AccessState->SubjectSecurityContext));

		// Выполняется запрос не на чтение?
		if (!IsSystem && !IsROAccessType(ParameterBlock))
		{
			// получаем полное имя файла
			status = FltGetFileNameInformation(Data, FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_ALWAYS_ALLOW_CACHE_LOOKUP, &pFileNameInformation);

			if (NT_SUCCESS(status))
			{
				UNICODE_STRING upcaseName = { 0 };
				upcaseName.Length = pFileNameInformation->Name.Length;
				upcaseName.MaximumLength = pFileNameInformation->Name.MaximumLength;
				upcaseName.Buffer = ExAllocatePoolWithTag(NonPagedPool, upcaseName.MaximumLength, MEM_TAG_INPUT_FNAME);

				if (upcaseName.Buffer != NULL)
				{
					// копируем путь в отдельную строку, так как нам ничего нельзя менять по MSDN в pFileNameInformation
					RtlCopyUnicodeString(&upcaseName, &pFileNameInformation->Name);

					// Преобразуем к верхнему регистру чтобы добиться нечувствительности к регистру
					RtlUpcaseUnicodeString(&upcaseName, &upcaseName, FALSE);

					// Выполняем сравнение по базе фильтров
					if (MatchInStringFilters(&upcaseName) == TRUE)
					{
						// Попадание
						UNICODE_STRING volName = { 0, 0, NULL };
						NTSTATUS statusVolName = GetVolumeName(FltObjects->Volume, &volName);

						if (NT_SUCCESS(statusVolName) && volName.Buffer != NULL)
						{
							PT_DBG_PRINT(PTDBG_TRACE_ROUTINES, ("It's working! Caught %wZ \n", FileObject->FileName));
							PT_DBG_PRINT(PTDBG_TRACE_ROUTINES, ("VolumeName: %wZ\n", volName));
							SetROAccess(ParameterBlock);

							// Завершаем операцию
							Data->IoStatus.Information = 0;
							Data->IoStatus.Status = STATUS_ACCESS_DENIED;

							status = FLT_PREOP_COMPLETE;

							ExFreePoolWithTag(volName.Buffer, MEM_TAG_VOLUME_NAME);
						}
					}

					ExFreePoolWithTag(upcaseName.Buffer, MEM_TAG_INPUT_FNAME);
				}

				FltReleaseFileNameInformation(pFileNameInformation);
			}
		}
	}


	return status;
}

FLT_POSTOP_CALLBACK_STATUS
ReadOnlynessPostOperation(
	_Inout_ PFLT_CALLBACK_DATA Data,
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_In_opt_ PVOID CompletionContext,
	_In_ FLT_POST_OPERATION_FLAGS Flags
)
{
	UNREFERENCED_PARAMETER(Data);
	UNREFERENCED_PARAMETER(FltObjects);
	UNREFERENCED_PARAMETER(CompletionContext);
	UNREFERENCED_PARAMETER(Flags);

	return FLT_POSTOP_FINISHED_PROCESSING;
}

NTSTATUS
ReadOnlynessInstanceQueryTeardown(
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_In_ FLT_INSTANCE_QUERY_TEARDOWN_FLAGS Flags
)
{
	PAGED_CODE();

	UNREFERENCED_PARAMETER(FltObjects);
	UNREFERENCED_PARAMETER(Flags);
	
	return STATUS_SUCCESS;
}

VOID
ReadOnlynessInstanceTeardownStart(
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_In_ FLT_INSTANCE_TEARDOWN_FLAGS Flags
)
{
	PAGED_CODE();

	UNREFERENCED_PARAMETER(FltObjects);
	UNREFERENCED_PARAMETER(Flags);
}

VOID
ReadOnlynessInstanceTeardownComplete(
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_In_ FLT_INSTANCE_TEARDOWN_FLAGS Flags
)
{
	PAGED_CODE();

	UNREFERENCED_PARAMETER(FltObjects);
	UNREFERENCED_PARAMETER(Flags);
}

NTSTATUS ConnectCallback(
	__in PFLT_PORT ClientPort,
	__in PVOID ServerPortCookie,
	__in_bcount(SizeOfContext) PVOID ConnectionContext,
	__in ULONG SizeOfContext,
	__deref_out_opt PVOID *ConnectionCookie
)
{
	PAGED_CODE();

	UNREFERENCED_PARAMETER(ConnectionCookie);
	UNREFERENCED_PARAMETER(SizeOfContext);
	UNREFERENCED_PARAMETER(ConnectionContext);
	UNREFERENCED_PARAMETER(ServerPortCookie);

	ASSERT(pClientPort == NULL);
	pClientPort = ClientPort;
	PT_DBG_PRINT(PTDBG_TRACE_ROUTINES,
		("ReadOnlyness!ConnectCallback: Entered.\n"));

	return STATUS_SUCCESS;
}

VOID DisconnectCallback(__in_opt PVOID ConnectionCookie)
{
	PAGED_CODE();

	UNREFERENCED_PARAMETER(ConnectionCookie);
	PT_DBG_PRINT(PTDBG_TRACE_ROUTINES,
		("ReadOnlyness!DisconnectCallback: Entered.\n"));

	FltCloseClientPort(pFilterHandle, &pClientPort);
}

NTSTATUS MessageCallback(
	__in PVOID ConnectionCookie,
	__in_bcount_opt(InputBufferSize) PVOID InputBuffer,
	__in ULONG InputBufferSize,
	__out_bcount_part_opt(OutputBufferSize, *ReturnOutputBufferLength) PVOID OutputBuffer,
	__in ULONG OutputBufferSize,
	__out PULONG ReturnOutputBufferLength
)
{
	PAGED_CODE();

	UNREFERENCED_PARAMETER(ReturnOutputBufferLength);
	UNREFERENCED_PARAMETER(OutputBuffer);
	UNREFERENCED_PARAMETER(OutputBufferSize);
	UNREFERENCED_PARAMETER(ConnectionCookie);

	NTSTATUS status;
	ROCommands command;
	USHORT RuleLength;
	PCHAR AnsiStr;

	if ((InputBuffer != NULL) && (InputBufferSize >= sizeof(S_ROCOMMAND)))
	{
		try 
		{
			command = ((PS_ROCOMMAND)InputBuffer)->Command;
			RuleLength = ((PS_ROCOMMAND)InputBuffer)->RuleLength;
			AnsiStr = &((PCHAR)InputBuffer)[sizeof(S_ROCOMMAND)];
		} 
		except(EXCEPTION_EXECUTE_HANDLER) 
		{
			return GetExceptionCode();
		}

		switch (command)
		{
		case FlushRules:
			ClearStringFilters();
			status = STATUS_SUCCESS;
			break;
		case AddRule:
			AddStringFilter(AnsiStr);
			status = STATUS_SUCCESS;
			break;
		default:
			status = STATUS_INVALID_PARAMETER;
			break;
		}
	}
	else
	{
		status = STATUS_INVALID_PARAMETER;
	}
	return status;
}