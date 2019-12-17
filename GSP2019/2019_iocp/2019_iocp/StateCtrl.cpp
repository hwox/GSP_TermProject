#include "StateCtrl.h"



StateCtrl::StateCtrl()
{
}


StateCtrl::~StateCtrl()
{
}

bool StateCtrl::IsDie(int hp)
{
	if (hp <= 0)
	{
		// Á×Àº°Å
		return true; 
	}
	return false; 
}
