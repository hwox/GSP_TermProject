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
		// ExecDirect가 실패했으면 에러코드를 출력
		HandleDiagnosticRecord(hstmt, SQL_HANDLE_STMT, retcode);
	}
	// Process data  
	if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
		// 더이상 안쓰면 cancel해서 돌려주고 이 밑에다 돌려주는 코드
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
		cout << "DB 업데이트 완료" << endl;
	}
	else {
		HandleDiagnosticRecord(hstmt, SQL_HANDLE_STMT, retcode);
	}

	if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
		// 더이상 안쓰면 cancel해서 돌려주고 이 밑에다 돌려주는 코드
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
		// ExecDirect가 실패했으면 에러코드를 출력
		HandleDiagnosticRecord(hstmt, SQL_HANDLE_STMT, retcode);
	}
	// Process data  
	if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
		// 더이상 안쓰면 cancel해서 돌려주고 이 밑에다 돌려주는 코드
		SQLCancel(hstmt);
		SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
	}
}




//void DataBase::UpdateDB(const string& id, short x, short y, char lv, short hp, short exp)
//{
//	//statement 처리에 대 한 상태 정보와 같은 statement 정보에 대한 액세스를 제공 합니다.
//	retcode = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);
//
//	//SQL 명령어 전송 받는건 다른명령어 사용해서. --> SQL PATCH써서. 그냥 패치 쓰면 실행결과 하나씩 꺼내고 끝임. 허공중에 꺼내서 버림. 
//	//얘는 오버랩드마냥 hstmt는 스레드마다 별도로 가지고있어야함. 
//	cout << "디비에 저장하는 값 x : " << x << " y: " << y << endl;
//	SQLLEN posX = x, posY = y;
//	//cout << "디비에 저장하는 값 x : " << posX << " y: " << y << endl;
//
//	wchar_t text[100];
//	TCHAR* wchar_id = (TCHAR*)id.c_str();
//	// 과제 5 TODO
//	// 1. 여기서 데이터를 입력받아 업데이트를 시킨다. 
//	wsprintf(text, L"EXEC SetPos %S, %d, %d, %d, %d, %d", wchar_id, x, y, lv, hp, exp);
//
//	retcode = SQLExecDirect(hstmt, (SQLWCHAR*)text, SQL_NTS);
//
//	if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO)
//	{
//		//꺼낸 데이터를 어디다가 연결을 해줘라!허공에 뿌리지마라
//		// Fetch and print each row of data. On an error, display a message and exit.  
//		//셀렉트 시 날아오는 데이터 수 알 수 없음. 루프돌면서하나하나 받아야함. 패치하는데 석세스가 아닐 때까지.
//		// LI거나 데이터가 없거나. LI면 에러난다. 에러아니면 석세스면 읽어서 화면에 출력 아니면 다읽었다하고 빠져나감. 
//
//		cout << "업데이트 내역: " << endl;
//		//wprintf(L"%s: %d %d %d\n", szName, m_xPos, m_yPos);
//		cout << szName << "디비업뎃 x: " << x << ", " << y << endl;
//		//retcode = SQLFetch(hstmt);
//		if (retcode == SQL_ERROR)
//		{
//			cout << "업데이트 오류: " << endl;
//
//			HandleDiagnosticRecord(hstmt, SQL_HANDLE_STMT, retcode);
//		}
//
//
//	}
//	else
//	{
//		//cout << "업데이트중 데이터 없음: " << endl;
//
//		HandleDiagnosticRecord(hstmt, SQL_HANDLE_STMT, retcode);
//
//	}
//	// Process data  
//	if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
//		SQLCancel(hstmt);
//		SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
//	}
//	cout << "업데이트 완료!" << endl;
//}