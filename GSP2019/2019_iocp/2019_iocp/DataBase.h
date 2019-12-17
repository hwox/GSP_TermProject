#pragma once
#include <windows.h>  
#include <stdio.h>  
#include <string.h>
#include <iostream>
#define UNICODE  
#include <sqlext.h>  
constexpr auto NAME_LEN = 11;
using namespace std;
class DataBase
{
	SQLHENV henv;
	SQLHDBC hdbc;
	SQLHSTMT hstmt = 0;
	SQLRETURN retcode;
	SQLINTEGER nID, nX, nY, nLevel, nHp, nExp;
	SQLWCHAR szName[NAME_LEN];
	SQLLEN cbName = 0, cbID = 0, cbX = 0, cbY = 0, cbLevel = 0, cbExp = 0, cbHp=0;

public:
	DataBase();
	~DataBase();
	void HandleDiagnosticRecord(SQLHANDLE hHandle, SQLSMALLINT hType, RETCODE RetCode);
	//void saveDB(const string& id, short x, short y, char lv, short hp, short exp);
	void saveDB(int id, short x, short y, int level, int exp, int hp);
	bool ForLoginCheckID(int id); // ·Î±×ÀÎ
	void GetInitInfo(int id, short *x, short *y, int *level, int *exp, int *hp);
};

