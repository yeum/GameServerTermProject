#include <iostream>
#include <fstream>

using namespace std;

int main()
{
	char caveMap[901];
	char caveMap2[901];
	char caveMap3[901];
	int count = 0;
	ifstream fin;

	fin.open("CaveMap.txt");
	while (fin.get(caveMap[count]))
	{
		++count;
	}
	fin.close();

	count = 0;
	fin.open("CaveMap2.txt");
	while (fin.get(caveMap2[count]))
	{
		++count;
	}
	fin.close();

	count = 0;
	fin.open("CaveMap3.txt");
	while (fin.get(caveMap3[count]))
	{
		++count;
	}
	fin.close();

	ofstream fout;

	fout.open("MapData.txt",ios::binary);

	count = 0;
	int first = 0;

	// ÀüÃ¼ ¸Ê
	for (int y = 0; y < 10; ++y)
	{
		++first;
		// ÇÑ ¸Ê ÁÙ
		if (first % 3 == 1)
		{
			for (int a = 0; a < 30; ++a)
			{
				// ÇÑ ¸Êµ¥ÀÌÅÍ ÁÙ
				for (int i = 0; i < 3; ++i)
				{
					for (int j = 0; j < 30; ++j)
					{
						fout << caveMap[(a * 30) + j];
					}
					for (int j = 0; j < 30; ++j)
					{
						fout << caveMap2[(a * 30) + j];
					}
					for (int j = 0; j < 30; ++j)
					{
						fout << caveMap3[(a * 30) + j];
					}
				}
				for (int j = 0; j < 30; ++j)
				{
					fout << caveMap[(a * 30) + j];
				}
			}
		}
		else if (first % 3 == 2)
		{
			for (int a = 0; a < 30; ++a)
			{
				for (int i = 0; i < 3; ++i)
				{
					for (int j = 0; j < 30; ++j)
					{
						fout << caveMap2[(a * 30) + j];
					}
					for (int j = 0; j < 30; ++j)
					{
						fout << caveMap3[(a * 30) + j];
					}
					for (int j = 0; j < 30; ++j)
					{
						fout << caveMap[(a * 30) + j];
					}
				}
				for (int j = 0; j < 30; ++j)
				{
					fout << caveMap2[(a * 30) + j];
				}
			}
		}
		else if(first % 3 == 0)
		{
			for (int a = 0; a < 30; ++a)
			{
				for (int i = 0; i < 3; ++i)
				{
					for (int j = 0; j < 30; ++j)
					{
						fout << caveMap3[(a * 30) + j];
					}
					for (int j = 0; j < 30; ++j)
					{
						fout << caveMap[(a * 30) + j];
					}
					for (int j = 0; j < 30; ++j)
					{
						fout << caveMap2[(a * 30) + j];
					}
				}
				for (int j = 0; j < 30; ++j)
				{
					fout << caveMap3[(a * 30) + j];
				}
			}
		}
	}
	fout.close();
	system("pause");
}