#include "DataBase.h"



DataBase::DataBase()
{
	retcode = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv);
	retcode = SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER*)SQL_OV_ODBC3, 0);
	retcode = SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc);

	retcode = SQLConnect(hdbc, (SQLWCHAR*)L"2016182016", SQL_NTS, (SQLWCHAR*)NULL, 0, NULL, 0);
	setlocale(LC_ALL, "korean");
}


DataBase::~DataBase()
{
	SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
	SQLFreeHandle(SQL_HANDLE_ENV, henv);
}

void DataBase::HandleDiagnosticRecord(SQLHANDLE hHandle, SQLSMALLINT hType, RETCODE RetCode)
{
	SQLSMALLINT iRec = 0;  SQLINTEGER	iError;
	WCHAR	wszMessage[1000];
	WCHAR	wszState[SQL_SQLSTATE_SIZE + 1];

	if (RetCode == SQL_INVALID_HANDLE) {
		fwprintf(stderr, L"Invalid handle!\n");  return;
	}
	while (SQLGetDiagRec(hType, hHandle, ++iRec, wszState, &iError, wszMessage, (SQLSMALLINT)(sizeof(wszMessage) / sizeof(WCHAR)), (SQLSMALLINT *)NULL) == SQL_SUCCESS) {
		// Hide data truncated..
		if (wcsncmp(wszState, L"01004", 5)) {
			fwprintf(stderr, L"[%5.5s] %s (%d)\n", wszState, wszMessage, iError);
		}
	}
}

bool DataBase::ForLoginCheckID(int id)
{
	retcode = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);

	//char change_id[10];
	//sprintf(change_id, "%d", id);
	wchar_t text[100];

	wsprintf(text, L"EXEC LoginID %d", id);

	retcode = SQLExecDirect(hstmt, (SQLWCHAR*)text, SQL_NTS);
	if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
		cout << "db success" << endl;
		return true;
	}
	else {
		// ExecDirect�� ���������� �����ڵ带 ���
		HandleDiagnosticRecord(hstmt, SQL_HANDLE_STMT, retcode);
	}
	// Process data  
	if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
		// ���̻� �Ⱦ��� cancel�ؼ� �����ְ� �� �ؿ��� �����ִ� �ڵ�
		SQLCancel(hstmt);
		SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
	}
	return false;
}
void DataBase::saveDB(int id, short x, short y, int level, int exp, int hp)
{
	retcode = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);
	wchar_t text[100];

	wsprintf(text, L"EXEC SaveData %d, %d, %d, %d, %d, %d", id, (int)x, (int)y, level, exp, hp);

	retcode = SQLExecDirect(hstmt, (SQLWCHAR*)text, SQL_NTS);
	if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
		cout << "DB ������Ʈ �Ϸ�" << endl;
	}
	else {
		HandleDiagnosticRecord(hstmt, SQL_HANDLE_STMT, retcode);
	}

	if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
		// ���̻� �Ⱦ��� cancel�ؼ� �����ְ� �� �ؿ��� �����ִ� �ڵ�
		SQLCancel(hstmt);
		SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
	}

}

void DataBase::GetInitInfo(int id, short *x, short *y, int *level, int *exp, int *hp)
{
	retcode = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);

	//char change_id[10];
	//sprintf(change_id, "%d", id);
	wchar_t text[100];

	wsprintf(text, L"EXEC GetInitInfor %d", id);

	//retcode = SQLExecDirect(hstmt, (SQLWCHAR*)L"EXEC LoginID 1001", SQL_NTS);
	retcode = SQLExecDirect(hstmt, (SQLWCHAR*)text, SQL_NTS);
	if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {

		retcode = SQLBindCol(hstmt, 1, SQL_C_LONG, &nX, 10, &cbX);
		retcode = SQLBindCol(hstmt, 2, SQL_C_LONG, &nY, 10, &cbY);
		retcode = SQLBindCol(hstmt, 3, SQL_C_LONG, &nLevel, 10, &cbLevel);
		retcode = SQLBindCol(hstmt, 4, SQL_C_LONG, &nExp, 10, &cbExp);
		retcode = SQLBindCol(hstmt, 5, SQL_C_LONG, &nHp, 10, &cbHp);
		retcode = SQLFetch(hstmt);
		if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
			wprintf(L" x: %d  y: %d / %d %d %d \n",  nX, nY, nLevel, nExp, nHp);
		}
		*x = (short)nX;
		*y = (short)nY;
		*level = nLevel;
		*exp = nExp;
		*hp = nHp;
	}
	else {
		// ExecDirect�� ���������� �����ڵ带 ���
		HandleDiagnosticRecord(hstmt, SQL_HANDLE_STMT, retcode);
	}
	// Process data  
	if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
		// ���̻� �Ⱦ��� cancel�ؼ� �����ְ� �� �ؿ��� �����ִ� �ڵ�
		SQLCancel(hstmt);
		SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
	}
}




//void DataBase::UpdateDB(const string& id, short x, short y, char lv, short hp, short exp)
//{
//	//statement ó���� �� �� ���� ������ ���� statement ������ ���� �׼����� ���� �մϴ�.
//	retcode = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);
//
//	//SQL ��ɾ� ���� �޴°� �ٸ���ɾ� ����ؼ�. --> SQL PATCH�Ἥ. �׳� ��ġ ���� ������ �ϳ��� ������ ����. ����߿� ������ ����. 
//	//��� �������帶�� hstmt�� �����帶�� ������ �������־����. 
//	cout << "��� �����ϴ� �� x : " << x << " y: " << y << endl;
//	SQLLEN posX = x, posY = y;
//	//cout << "��� �����ϴ� �� x : " << posX << " y: " << y << endl;
//
//	wchar_t text[100];
//	TCHAR* wchar_id = (TCHAR*)id.c_str();
//	// ���� 5 TODO
//	// 1. ���⼭ �����͸� �Է¹޾� ������Ʈ�� ��Ų��. 
//	wsprintf(text, L"EXEC SetPos %S, %d, %d, %d, %d, %d", wchar_id, x, y, lv, hp, exp);
//
//	retcode = SQLExecDirect(hstmt, (SQLWCHAR*)text, SQL_NTS);
//
//	if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO)
//	{
//		//���� �����͸� ���ٰ� ������ �����!����� �Ѹ�������
//		// Fetch and print each row of data. On an error, display a message and exit.  
//		//����Ʈ �� ���ƿ��� ������ �� �� �� ����. �������鼭�ϳ��ϳ� �޾ƾ���. ��ġ�ϴµ� �������� �ƴ� ������.
//		// LI�ų� �����Ͱ� ���ų�. LI�� ��������. �����ƴϸ� �������� �о ȭ�鿡 ��� �ƴϸ� ���о����ϰ� ��������. 
//
//		cout << "������Ʈ ����: " << endl;
//		//wprintf(L"%s: %d %d %d\n", szName, m_xPos, m_yPos);
//		cout << szName << "������ x: " << x << ", " << y << endl;
//		//retcode = SQLFetch(hstmt);
//		if (retcode == SQL_ERROR)
//		{
//			cout << "������Ʈ ����: " << endl;
//
//			HandleDiagnosticRecord(hstmt, SQL_HANDLE_STMT, retcode);
//		}
//
//
//	}
//	else
//	{
//		//cout << "������Ʈ�� ������ ����: " << endl;
//
//		HandleDiagnosticRecord(hstmt, SQL_HANDLE_STMT, retcode);
//
//	}
//	// Process data  
//	if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
//		SQLCancel(hstmt);
//		SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
//	}
//	cout << "������Ʈ �Ϸ�!" << endl;
//}