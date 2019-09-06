#pragma once
#include "Scene.h"
#include "Network.h"
#include <memory>
class SceneManager {
private:
	Network network;
	Scene * pSceneList[10];
	int PreIndex;
public:
	SceneManager();
	~SceneManager();
public:
	void Entry(int index, Scene* scene);
	void Warp(int index);
public:
	Scene* getScene()
	{
		return pSceneList[PreIndex];
	}

	Network* getNetwork()
	{
		return &network;
	}

};