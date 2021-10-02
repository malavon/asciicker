#ifndef GAMEPAD_H
#define GAMEPAD_H

void LoadGamePad();
void FreeGamePad();

void ConnectGamePad(const char* name, int axes, int buttons, const int16_t axis[]=0, const int16_t button[]=0);
void DisconnectGamePad();

void UpdateGamePadAxis(int a, int16_t pos);
void UpdateGamePadButton(int b, int16_t pos);


void PaintGamePad(AnsiCell* ptr, int width, int height);

#endif
