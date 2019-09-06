#include "FWMain.h"
FWMain*	 FWMain::instance = NULL;
FWMain::FWMain() : FWWindows("Simple Chess",800,850)
{
	//Entry(0, new Chess);
	Entry(0, new Chess_iocp);
	Warp(0);
}
FWMain::~FWMain()
{

}
