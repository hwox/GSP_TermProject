#pragma once
#include <windows.h>  
#include <stdio.h>  
#include <string.h>
#include <iostream>

class StateCtrl
{
public:
	StateCtrl();
	~StateCtrl();
	bool IsDie(int hp);
};

