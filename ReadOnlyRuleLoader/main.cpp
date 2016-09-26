
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <Windows.h>
#include "fltWinXPCompat.h"

#include "..\ReadOnlyness\Common.h"


// точка входа
int main(void)
{
	// открываем input.txt и загружаем через порт коммуникации все указанные там строки правил
	std::string line;
	std::ifstream if_file("input.txt");
	std::vector<char> buffer;

	S_ROCOMMAND Cmd;
	HANDLE Port = 0;
	DWORD BytesReturned = 0;
	char *pCmd = (char*)&Cmd;

	// файл открыт?
	if (!if_file.is_open())
	{
		// Ошибка
		std::cout << "Cannot open input.txt" << std::endl;
		return -1;
	}
	std::cout << "File input.txt opened" << std::endl;

	// пытаемся подключиться к драйверу
	if (FilterConnectCommunicationPort(READONLYNESS_PORT_NAME, 0, NULL, 0, NULL, &Port) != S_OK)
	{
		std::cout << "Cannot open driver port for connection. Maybe driver is not installed?" << std::endl;
		return -1;
	}
	std::cout << "Connected to driver" << std::endl;

	// выполняем очистку от всех правил
	Cmd.Command = FlushRules;
	
	if (FilterSendMessage(Port, &Cmd, sizeof(S_ROCOMMAND), NULL, 0, &BytesReturned) != S_OK)
	{
		std::cout << "Cannot send to driver port. LastError = " << GetLastError();
		return -1;
	}
	std::cout << "Driver rule list flushed\nLoading rules...\n" << std::endl;

	// загружаем правила (команда загрузки)
	Cmd.Command = AddRule;

	while(!if_file.eof())
	{
		// очищаем буфер
		buffer.clear();

		// читаем строку из файла
		std::getline(if_file, line);

		// Записываем ее размер
		Cmd.RuleLength = (USHORT) line.size();

		// добавляем заголовок в начало
		buffer.insert(buffer.begin(), pCmd, (pCmd) + sizeof(S_ROCOMMAND));

		// добавляем в конец данные строки
		buffer.insert(buffer.end(), line.cbegin(), line.cend());

		if (FilterSendMessage(Port, &buffer[0], buffer.size(), NULL, 0, &BytesReturned) != S_OK)
		{
			std::cout << "Cannot send to driver port. LastError = " << GetLastError();
			return -1;
		}

		std::cout << "Loading rule: " << line << std::endl;
	}
	
	std::cout << "Rules loaded";

	CloseHandle(Port);

	return 0;
}