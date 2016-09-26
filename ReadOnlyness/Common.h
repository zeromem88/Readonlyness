#pragma once

#define READONLYNESS_PORT_NAME L"\\READONLYNESS"

// типы команд
typedef enum _ROCommands
{
	FlushRules = 1,
	AddRule = 2
} ROCommands;

// структура команды
typedef struct _S_ROCOMMAND
{
	// тип команды
	ROCommands Command;
	// длина строки правила (правило передается как ANSI CHAR)
	USHORT RuleLength;
} S_ROCOMMAND, *PS_ROCOMMAND;