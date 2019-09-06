#pragma once
#include "FWWindows.h"
//#include "Chess.h"
#include "Chess_iocp.h"
class FWMain : public FWWindows
{
private:
	static FWMain* instance;
public:
	static FWMain& getMain()
	{
		if (instance == NULL)
		{
			instance = new FWMain;
		}
		return *instance;
	}
public:
	FWMain();
	~FWMain();
};