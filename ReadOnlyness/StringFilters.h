#pragma once

#include "CommonKernel.h"
#include "Helper.h"

/// Структура записи списка строковых фильтров для поиска совпадений
DECLSPEC_ALIGN(MEMORY_ALLOCATION_ALIGNMENT) typedef struct _SSTRING_FILTER_ITEM
{
	PVOID Next;
	// маска для проверки на совпадение
	PUNICODE_STRING Mask;
} SSTRING_FILTER_ITEM;

typedef SSTRING_FILTER_ITEM * PSSTRING_FILTER_ITEM;

/// Инициализация списка фильтр-строк
void InitStringFilters();

/// Добавление строки-фильтра
NTSTATUS AddStringFilter(PCHAR FilterString);

/// Очистка всех фильтров
void ClearStringFilters();

/// Деинициализация списка фильтр-строк
void DeinitStringFilters();

/// Поиск в совпадениях
BOOLEAN MatchInStringFilters(PUNICODE_STRING FileName);

/// Сравнение двух строк по шаблону
BOOLEAN WildTextCompare(
	PWCH pTameText,   // A string without wildcards
	PWCH pWildText    // A (potentially) corresponding string with wildcards
);