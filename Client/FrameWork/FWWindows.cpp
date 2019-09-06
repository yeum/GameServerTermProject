#include "FWWindows.h"

FWWindows::FWWindows(const char* WindowsName, int WindowsSizeX , int WindowsSizeY)
{
	int Len = strlen(WindowsName) + 1;
	this->WindowsName = new char[Len];
	strcpy_s(this->WindowsName, Len, WindowsName);
	this->WindowsSizeX = WindowsSizeX;
	this->WindowsSizeY = WindowsSizeY;
}
FWWindows::~FWWindows()
{
	SAFE_DELETE_ARRAY(this->WindowsName);
}