#pragma once
#include <Windows.h>
const int MAX_BMP = 10;
 struct Data
{
	HBITMAP Bitmap;					// �̹��� ��ü
	int width;						// �̹����� ���γʺ�
	int height;						// �̹����� ���γ���
	int x;
	int y;
};
class Sprite
{
private:
	int  Index;						// ����ڰ� �迭�� �ε��� ��������Ʈ �̹����� �� ����
	Data BmpData[MAX_BMP];				// �ִϸ��̼ǿ� �ʿ��� �̹���'��'�� ���� �� �ִ� �迭
public:
	Sprite();
	~Sprite();

public:
	void Entry(int Num, const char* path, int x, int y);		// �̹��� �ε�

public:
	void Render(HDC* hdc, int Num);		// �ִϸ��̼��� �׸��� ���� ����
	void Render(HDC* hdc, int Num, int x, int y);		// ��ǥ�� ���� ����
	void Render(HDC* hdc, int Num, UINT color);
	void Render(HDC* hdc, int Num, UINT color, int x, int y);
	void Render(HDC* hdc, int Num, float a);
	void Render(HDC* hdc, int Num, UINT color, float a);

public:
	void setLocation(int Num, int x, int y);
public:
	int getX(int Num);
	int getY(int Num);
	int getWidth(int Num);
	int getHeight(int Num);
	int getIndex();
};

