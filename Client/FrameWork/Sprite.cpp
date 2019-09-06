#include "Sprite.h"

Sprite::Sprite()
{
	Index = 0;				// 왜 초기화를 생성자에서?
}
Sprite::~Sprite()
{
	for (int i = 0; i < MAX_BMP; ++i)
	{
		if (BmpData[i].Bitmap != NULL)
			DeleteObject(BmpData[i].Bitmap);
	}
}
void Sprite::Entry(int Num, const char* path, int x, int y)
{
	BITMAP TmpBit;					// 단지 이미지의 높이와 너비를 받아서 넘겨주기위한 변수
	BmpData[Num].Bitmap = (HBITMAP)LoadImage(NULL, path, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);		// 해당 경로의 이미지를 가져와 변수에 집어넣기
	GetObject(BmpData[Num].Bitmap, sizeof(BITMAP), &TmpBit);
	BmpData[Num].height = TmpBit.bmHeight;
	BmpData[Num].width = TmpBit.bmWidth;
	BmpData[Num].x = x;
	BmpData[Num].y = y;
	//GetObject(BmpData->Bitmap, sizeof(int), &BmpData[Num].width);
	Index++;		// 스프라이트 이미지의 총 개수를 올려준다.
}
void Sprite::Render(HDC* hdc, int Num)
{
	HDC TmpDC = CreateCompatibleDC(*hdc);
	HBITMAP m_oldbitmap = (HBITMAP)SelectObject(TmpDC, BmpData[Num].Bitmap);
	BitBlt(*hdc, BmpData[Num].x, BmpData[Num].y, BmpData[Num].width, BmpData[Num].height, TmpDC, 0, 0, SRCCOPY);
	DeleteDC(TmpDC);
	DeleteObject(m_oldbitmap);
}

void Sprite::Render(HDC* hdc, int Num, int x, int y)
{
	HDC TmpDC = CreateCompatibleDC(*hdc);
	HBITMAP m_oldbitmap = (HBITMAP)SelectObject(TmpDC, BmpData[Num].Bitmap);
	BitBlt(*hdc, x, y, BmpData[Num].width, BmpData[Num].height, TmpDC, 0, 0, SRCCOPY);
	DeleteDC(TmpDC);
	DeleteObject(m_oldbitmap);
}

void Sprite::Render(HDC* hdc, int Num, UINT color)
{

	HDC TmpDC = CreateCompatibleDC(*hdc);
	HBITMAP m_oldbitmap = (HBITMAP)SelectObject(TmpDC, BmpData[Num].Bitmap);
	TransparentBlt(*hdc, BmpData[Num].x, BmpData[Num].y, BmpData[Num].width, BmpData[Num].height, TmpDC, 0, 0, BmpData[Num].width, BmpData[Num].height, color);

	DeleteDC(TmpDC);
	DeleteObject(m_oldbitmap);
}

void Sprite::Render(HDC* hdc, int Num, UINT color, int x, int y)
{

	HDC TmpDC = CreateCompatibleDC(*hdc);
	HBITMAP m_oldbitmap = (HBITMAP)SelectObject(TmpDC, BmpData[Num].Bitmap);
	TransparentBlt(*hdc, x, y, BmpData[Num].width, BmpData[Num].height, TmpDC, 0, 0, BmpData[Num].width, BmpData[Num].height, color);

	DeleteDC(TmpDC);
	DeleteObject(m_oldbitmap);
}

void Sprite::Render(HDC* hdc, int Num, UINT color, float a)
{
	BLENDFUNCTION bf;

	bf.AlphaFormat = 0;
	bf.BlendFlags = 0;
	bf.BlendOp = AC_SRC_OVER;
	bf.SourceConstantAlpha = (int)(a * 255.0f);

	HDC TmpDC = CreateCompatibleDC(*hdc);
	HDC AlphaTmpDC = CreateCompatibleDC(*hdc);
	HBITMAP hBackBit = CreateCompatibleBitmap(*hdc, BmpData[Num].width, BmpData[Num].height);
	HBITMAP m_oldbitmap = (HBITMAP)SelectObject(AlphaTmpDC, hBackBit);
	HBITMAP m_oldbitmap1 = (HBITMAP)SelectObject(TmpDC, BmpData[Num].Bitmap);

	BitBlt(AlphaTmpDC, 0, 0, BmpData[Num].width, BmpData[Num].height, *hdc, BmpData[Num].x, BmpData[Num].y, SRCCOPY);
	TransparentBlt(AlphaTmpDC, 0, 0, BmpData[Num].width, BmpData[Num].height, TmpDC, 0, 0, BmpData[Num].width, BmpData[Num].height, color);
	AlphaBlend(*hdc, BmpData[Num].x, BmpData[Num].y, BmpData[Num].width, BmpData[Num].height, AlphaTmpDC, 0, 0, BmpData[Num].width, BmpData[Num].height,bf);
	

	//AlphaBlend()


	DeleteDC(TmpDC);
	DeleteDC(AlphaTmpDC);
	DeleteObject(m_oldbitmap);
	DeleteObject(m_oldbitmap1);
	DeleteObject(hBackBit);

}
void Sprite::Render(HDC* hdc, int Num, float a)
{
	BLENDFUNCTION bf;

	bf.AlphaFormat = 0;
	bf.BlendFlags = 0;
	bf.BlendOp = AC_SRC_OVER;
	bf.SourceConstantAlpha = (int)(a * 255.0f);
	HDC AlphaTmpDC = CreateCompatibleDC(*hdc);
	HBITMAP hBackBit = (HBITMAP)SelectObject(AlphaTmpDC, BmpData[Num].Bitmap);

	AlphaBlend(*hdc, BmpData[Num].x, BmpData[Num].y, BmpData[Num].width, BmpData[Num].height, AlphaTmpDC, 0, 0, BmpData[Num].width, BmpData[Num].height, bf);
	DeleteDC(AlphaTmpDC);
	DeleteObject(hBackBit);

}
void Sprite::setLocation(int Num, int x, int y)
{
	BmpData[Num].x = x;
	BmpData[Num].y = y;
}

int Sprite::getX(int Num) { return BmpData[Num].x; }
int Sprite::getY(int Num) { return BmpData[Num].y; }
int Sprite::getWidth(int Num) { return BmpData[Num].width; }
int Sprite::getHeight(int Num) { return BmpData[Num].height; }
int Sprite::getIndex() { return Index; }

