#include "SceneManager.h"

SceneManager::SceneManager()
{
	PreIndex = 0;
	for (int i = 0; i < 10; ++i)
		pSceneList[i] = NULL;
}
SceneManager::~SceneManager()
{
	for (int i = 0; i < 10; ++i)
		SAFE_DELETE(pSceneList[i]);
}
void SceneManager::Entry(int index, Scene* scene)
{
	pSceneList[index] = scene;
}

void SceneManager::Warp(int index)
{
	pSceneList[PreIndex]->Destroy();
	PreIndex = index;
	pSceneList[PreIndex]->setNetwork(&network);
	pSceneList[PreIndex]->Enter();
}
