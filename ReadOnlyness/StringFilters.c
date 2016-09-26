#include "StringFilters.h"

// Первый элемент списка
volatile PSSTRING_FILTER_ITEM ListStart = NULL;
// Последний элемент списка
volatile PSSTRING_FILTER_ITEM ListEnd = NULL;
// Спиглок для синхронизации доступа к спискам
KSPIN_LOCK filtersSpinLock;

/// инициализиация списка фильтров
void InitStringFilters()
{
	// инициализация синхронизации доступа
	KeInitializeSpinLock(&filtersSpinLock);
}

/// Добавление строки-фильтра (C:\Program 
NTSTATUS AddStringFilter(PCHAR FilterString)
{
	// статус операции
	NTSTATUS ntStatus = STATUS_UNSUCCESSFUL;

	// строка текста ANSI
	ANSI_STRING AnsiStr;

	// Здесь будет UNICODE версия
	PUNICODE_STRING pUniStr = NULL;

	// Здесь будет разыменованная символическая ссылка буквы диска
	PUNICODE_STRING DosLink = NULL;

	// Размер буфера для преобразования
	USHORT UniStrBuffSize = 0;

	if ((FilterString[0] >= 'A' && FilterString[0] <= 'Z') || (FilterString[0] >= 'a' && FilterString[0] <= 'z'))
	{
		WCHAR DosDiskLetter = (WCHAR)FilterString[0];
		DosDiskLetter = RtlUpcaseUnicodeChar(DosDiskLetter);

		if (FilterString[1] == ':')
		{
			// первый символ - буква диска, разыменовываем
			ntStatus = GetDosDeviceName(DosDiskLetter, &DosLink);

			if (!NT_SUCCESS(ntStatus))
			{
				return ntStatus;
			}

			UniStrBuffSize = DosLink->Length;
		}
	}

	try
	{
		// Была обнаружена буква диска?
		if (DosLink != NULL)
		{
			// да, не учитываем ее
			RtlInitAnsiString(&AnsiStr, &FilterString[2]);
		}
		else
		{
			// строка формата без имени диска
			RtlInitAnsiString(&AnsiStr, FilterString);
		}

		UniStrBuffSize += (USHORT)RtlAnsiStringToUnicodeSize(&AnsiStr);

		// Защита от пустых значений
		if (UniStrBuffSize <= sizeof(UNICODE_NULL))
		{
			return ntStatus;
		}

		pUniStr = ExAllocatePoolWithTag(NonPagedPool, sizeof(UNICODE_STRING), MEM_TAG_STRFILTER_NAME);

		if (pUniStr != NULL)
		{
			pUniStr->Buffer = ExAllocatePoolWithTag(NonPagedPool, UniStrBuffSize + sizeof(UNICODE_NULL), MEM_TAG_STRFILTER_NAME);

			if (pUniStr->Buffer != NULL)
			{
				UNICODE_STRING convertedStr;
				RtlZeroMemory(&convertedStr, sizeof(UNICODE_STRING));

				// устанавливаем размеры буфера
				pUniStr->Length = 0;
				pUniStr->MaximumLength = UniStrBuffSize + sizeof(UNICODE_NULL);

				// копируем разыменованную ссылку буквы диска при необходимости
				if (DosLink != NULL)
				{
					ntStatus = RtlAppendUnicodeStringToString(pUniStr, DosLink);
				}
				else
				{
					ntStatus = STATUS_SUCCESS;
				}

				if (NT_SUCCESS(ntStatus))
				{
					// конвертируем строку в юникод
					ntStatus = RtlAnsiStringToUnicodeString(&convertedStr, &AnsiStr, TRUE);

					if (NT_SUCCESS(ntStatus))
					{
						// добавляем к разыменованному имени диска
						ntStatus = RtlAppendUnicodeStringToString(pUniStr, &convertedStr);

						if (NT_SUCCESS(ntStatus))
						{
							// создаем объект списка и добавляем его
							PSSTRING_FILTER_ITEM NewFilterItem = ExAllocatePoolWithTag(NonPagedPool, sizeof(SSTRING_FILTER_ITEM), MEM_TAG_STRFILTER_NAME);

							if (NewFilterItem != NULL)
							{
								KIRQL OldIrql;

								RtlZeroMemory(NewFilterItem, sizeof(SSTRING_FILTER_ITEM));
								RtlUpcaseUnicodeString(pUniStr, pUniStr, FALSE);

								NewFilterItem->Mask = pUniStr;

								KeAcquireSpinLock(&filtersSpinLock, &OldIrql);
								{
									if (ListStart == NULL)
									{
										// инициализируем начало и конец списка
										ListStart = NewFilterItem;
										ListEnd = NewFilterItem;
									}
									else
									{
										ListEnd->Next = NewFilterItem;
										ListEnd = NewFilterItem;
									}
								}
								KeReleaseSpinLock(&filtersSpinLock, OldIrql);

								ntStatus = STATUS_SUCCESS;
							}
							else
							{
								ntStatus = STATUS_INSUFFICIENT_RESOURCES;
								PT_DBG_PRINT(PTDBG_TRACE_ROUTINES, ("ReadOnlyness!StringFilter!AddStringFilter memory allocation error for SSTRING_FILTER_ITEM(size=%u)", (int)sizeof(SSTRING_FILTER_ITEM)));
							}
						}

						RtlFreeUnicodeString(&convertedStr);
					}
				}
			}
			else
			{
				ntStatus = STATUS_INSUFFICIENT_RESOURCES;
				PT_DBG_PRINT(PTDBG_TRACE_ROUTINES, ("ReadOnlyness!StringFilter!AddStringFilter memory allocation error for unicode string buffer(size=%u)", UniStrBuffSize));
			}
		}
		else
		{
			ntStatus = STATUS_INSUFFICIENT_RESOURCES;
			PT_DBG_PRINT(PTDBG_TRACE_ROUTINES, ("ReadOnlyness!StringFilter!AddStringFilter memory allocation error for UNICODE_STRING"));
		}
	}
	finally
	{
		if (DosLink != NULL)
		{
			ExFreePoolWithTag(DosLink->Buffer, MEM_TAG_OBJECT_NAME);
		}

		// при ошибке подчищаем ресурсы
		if (ntStatus != STATUS_SUCCESS)
		{
			if (pUniStr != NULL)
			{
				if (pUniStr->Buffer != NULL)
				{
					ExFreePoolWithTag(pUniStr->Buffer, MEM_TAG_STRFILTER_NAME);
				}

				ExFreePoolWithTag(pUniStr, MEM_TAG_STRFILTER_NAME);
			}
		}
	}

	return ntStatus;
}

/// Очистка всех фильтров
void ClearStringFilters()
{
	PSSTRING_FILTER_ITEM item = ListStart;
	KIRQL OldIrql;
	PVOID ItemReleasePointer = NULL;

	while (item != NULL)
	{
		KeAcquireSpinLock(&filtersSpinLock, &OldIrql);
		{
			ListStart = item->Next;
			if (ListStart == NULL)
			{
				ListEnd = NULL;
			}
		}
		KeReleaseSpinLock(&filtersSpinLock, OldIrql);

		// освобождаем память из-под строки фильтра
		ExFreePoolWithTag(item->Mask->Buffer, MEM_TAG_STRFILTER_NAME);

		// освобождаем память из-под структуры строки
		ExFreePoolWithTag(item->Mask, MEM_TAG_STRFILTER_NAME);

		// Сохраняем указатель на саму структуру элемента списка и переходим на след. элемент до освобождения памяти структуры
		ItemReleasePointer = item;
		item = item->Next;

		// освобождаем память всей структуры записи списка
		ExFreePoolWithTag(ItemReleasePointer, MEM_TAG_STRFILTER_NAME);
	}
}

/// деинициализация списков фильтров
void DeinitStringFilters()
{
	ClearStringFilters();
}

BOOLEAN MatchInStringFilters(PUNICODE_STRING FileName)
{
	BOOLEAN Result = FALSE;
	if (ListStart == NULL)
	{
		return Result;
	}

	PSSTRING_FILTER_ITEM item = ListStart;
	
	while (item != NULL)
	{
		Result = WildTextCompare(FileName->Buffer, item->Mask->Buffer);
		if (Result == TRUE)
		{
			break;
		}

		item = item->Next;
	}

	return Result;
}

/// Адаптированный алгоритм совпадений по *, ? от DrBoo для работы с UNICODE 
BOOLEAN WildTextCompare(PWCH pTameText, PWCH pWildText)
{
	PWCH pTameBookmark = (PWCH)0;
	PWCH pWildBookmark = (PWCH)0;

	while (1)
	{
		if (*pWildText == '*')
		{
			while (*(++pWildText) == '*')
			{
			}                          

			if (!*pWildText)
			{
				return TRUE;           
			}

			if (*pWildText != '?')
			{
				while (*pTameText != *pWildText)
				{
					if (!(*(++pTameText)))
						return FALSE;  
				}
			}

			pWildBookmark = pWildText;
			pTameBookmark = pTameText;
		}
		else if (*pTameText != *pWildText && *pWildText != '?')
		{
			if (pWildBookmark)
			{
				if (pWildText != pWildBookmark)
				{
					pWildText = pWildBookmark;

					if (*pTameText != *pWildText)
					{
						pTameText = ++pTameBookmark;
						continue;      
					}
					else
					{
						pWildText++;
					}
				}

				if (*pTameText)
				{
					pTameText++;
					continue;         
				}
			}

			return FALSE;              
		}

		pTameText++;
		pWildText++;

		if (!*pTameText)
		{
			while (*pWildText == '*')
			{
				pWildText++;           
			}

			if (!*pWildText)
			{
				return TRUE;           
			}

			return FALSE;              
		}
	}
}