#pragma once
#include <string.h>
#include "SceneManager.h"
class FWWindows : public SceneManager
{
private:
	char* WindowsName;
	int   WindowsSizeX;
	int   WindowsSizeY;
protected:
	FWWindows(const char* WindowsName = "Game",int WindowsSizeX = 1024,int WindowsSizeY = 768);
public:
	virtual ~FWWindows();


public:
#pragma region get
	char* getWindowsName() const
	{
		return WindowsName;
	}
	int getWindowsSizeX() const
	{
		return WindowsSizeX;
	}
	int getWindowsSizeY() const
	{
		return WindowsSizeY;

	}

#pragma endregion

};