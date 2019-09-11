#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <fstream>
#include <string>
using namespace std;

enum EditorMode { tiles, walkability };

// a panel is a logical subsection of the editor screen, i.e., this program's screen has a map panel and a palette panel.
const int mapPanelWidth = 80;
const int mapPanelHeight = 25;
const int palettePanelWidth = 80;
const int palettePanelHeight = 7;
const WORD defaultPaletteAttributes = BACKGROUND_BLUE | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
const int screenWidth = mapPanelWidth > palettePanelWidth ? mapPanelWidth : palettePanelWidth;
const int screenHeight = mapPanelHeight + palettePanelHeight;
const COORD screenSize = { screenWidth, screenHeight };
const COORD topLeft = { 0, 0 };
const COORD bufferSize = { 1, 1 };
const string mapFileName = "test.map";

// transient data for editor state
EditorMode editorMode = tiles;
SMALL_RECT screenRect = { 0, 0, screenWidth - 1, screenHeight - 1 };

// transient data for mouse painting
bool hasSelection = false;
CHAR_INFO selection = { 0 };
short mouseX = 0;
short mouseY = 0;

// transient data for IO
HANDLE stdInput;
HANDLE stdOutput;

// transient map data
bool walkabilityGrid[mapPanelHeight][mapPanelWidth]; // holds walkability data. true == walkable.
CHAR_INFO mapBuffer[mapPanelHeight][mapPanelWidth] = { {0} }; // holds visible characters, foreground color, background color.
CHAR_INFO screenBuffer[screenHeight][screenWidth] = { {0} }; // this buffer holds all data needed for the screen (map + palette).

void SetTitle()
{
    switch (editorMode)
    {
    case tiles:
        SetConsoleTitle(TEXT("Map Editor - Tile Mode"));
        break;
    case walkability:
        SetConsoleTitle(TEXT("Map Editor - Walkability Mode"));
        break;
    }
}

void InitConsole()
{
    SetTitle();

    stdInput = GetStdHandle(STD_INPUT_HANDLE);
    stdOutput = GetStdHandle(STD_OUTPUT_HANDLE);

    // hide the flashing cursor in the console window
    CONSOLE_CURSOR_INFO cursor;
    GetConsoleCursorInfo(stdOutput, &cursor);
    cursor.bVisible = 0;
    SetConsoleCursorInfo(stdOutput, &cursor);

    // enable mouse input for our window.
    // Windows 10 console will not receive mouse events without setting extended flags first,
    // see: https://stackoverflow.com/questions/42213161/console-mouse-input-not-working
    SetConsoleMode(stdInput, ENABLE_EXTENDED_FLAGS);
    SetConsoleMode(stdInput, ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT);

    // set console to exact width and height
    SetConsoleWindowInfo(stdOutput, true, &screenRect);
}

void InitWalkabilityGrid()
{
    for (int y = 0; y < mapPanelHeight; ++y)
    {
        for (int x = 0; x < mapPanelWidth; ++x)
        {
            walkabilityGrid[y][x] = true;
        }
    }
}

void InitPalette()
{
    selection.Attributes = defaultPaletteAttributes;

    for (int y = 0; y < palettePanelHeight; ++y)
    {
        for (int x = 0; x < palettePanelWidth; ++x)
        {
            int index = x + y * palettePanelWidth;

            if (index < 256)
            {
                screenBuffer[mapPanelHeight + y][x].Char.AsciiChar = (char)index;
                screenBuffer[mapPanelHeight + y][x].Attributes = defaultPaletteAttributes;
            }
            else if (index < 512)
            {
                screenBuffer[mapPanelHeight + y][x].Char.AsciiChar = '$';
                screenBuffer[mapPanelHeight + y][x].Attributes = (char)(index % 256);
            }
        }
    }
}

void SetPaletteAttributes(WORD attributes)
{
    for (int i = 0; i < 256; ++i)
    {
        screenBuffer[mapPanelHeight + (i / palettePanelWidth)][i % palettePanelWidth].Attributes = attributes;
    }
}

void Load(string fileName)
{
    ifstream infile(fileName, ifstream::binary);

    //get length of file (measured in bytes)
    infile.seekg(0, infile.end);
    int length = (int)infile.tellg();
    infile.seekg(0, infile.beg);

    // read file into a temporary buffer
    char* currentMapData = new char[length];
    infile.read(currentMapData, length);

    // read from temporary buffer into map buffer.
    for (int y = 0; y < mapPanelHeight; ++y)
    {
        for (int x = 0; x < mapPanelWidth; ++x)
        {
            mapBuffer[y][x].Char.AsciiChar = currentMapData[(x + y * screenWidth) * 3];
            mapBuffer[y][x].Attributes = currentMapData[(x + y * screenWidth) * 3 + 1];
            walkabilityGrid[y][x] = currentMapData[(x + y * screenWidth) * 3 + 2] == 0 ? false : true;
        }
    }

    delete[] currentMapData;
}

void Save(string fileName)
{
    char buffer[mapPanelHeight * mapPanelWidth * 3];

    int index = 0;
    for (int y = 0; y < mapPanelHeight; ++y)
    {
        for (int x = 0; x < mapPanelWidth; ++x)
        {
            buffer[(x + y * mapPanelWidth) * 3] = mapBuffer[y][x].Char.AsciiChar;
            buffer[(x + y * mapPanelWidth) * 3 + 1] = (char)(mapBuffer[y][x].Attributes % 256);
            buffer[(x + y * mapPanelWidth) * 3 + 2] = (char)(walkabilityGrid[y][x] ? 1 : 0);
        }
    }

    ofstream mapFile(fileName, ofstream::binary);
    mapFile.write(buffer, mapPanelHeight * mapPanelWidth * 3);
    mapFile.close();

    MessageBox(nullptr, "File Saved.", "", 0);
}

void selectFromPalette()
{
    hasSelection = true;

    CHAR_INFO charAtMouse;
    SMALL_RECT rect = { mouseX, mouseY, mouseX, mouseY };
    ReadConsoleOutput(stdOutput, &charAtMouse, bufferSize, topLeft, &rect);

    if ((mouseY == 28 && mouseX >= 16) || mouseY > 28) //x = 16, y = 28 is start of color palette.
    {
        selection.Attributes = screenBuffer[mouseY][mouseX].Attributes;
        SetPaletteAttributes(selection.Attributes);
    }
    else // just get the character
    {
        selection.Char.AsciiChar = screenBuffer[mouseY][mouseX].Char.AsciiChar;
    }
}

void HandleInputTileMode(INPUT_RECORD& inputRecord)
{
    if (inputRecord.EventType == KEY_EVENT)
    {
        if (inputRecord.Event.KeyEvent.bKeyDown)
        {
            switch (inputRecord.Event.KeyEvent.wVirtualKeyCode)
            {
            case VK_TAB:
                editorMode = walkability;
                SetTitle();
                break;
            case 'S':
                Save(mapFileName);
                break;
            }
        }
    }
    else if (inputRecord.EventType == MOUSE_EVENT)
    {
        switch (inputRecord.Event.MouseEvent.dwButtonState)
        {
        case FROM_LEFT_1ST_BUTTON_PRESSED:
            if (mouseY >= mapPanelHeight)
            {
                selectFromPalette();
            }
            else if (hasSelection)
            {
                mapBuffer[mouseY][mouseX] = selection;
            }
            break;
        case RIGHTMOST_BUTTON_PRESSED:
            hasSelection = false;
            SetPaletteAttributes(defaultPaletteAttributes);
            break;
        }
    }
}

void HandleInputWalkabilityMode(INPUT_RECORD& inputRecord)
{
    if (inputRecord.EventType == KEY_EVENT)
    {
        if (inputRecord.Event.KeyEvent.bKeyDown)
        {
            switch (inputRecord.Event.KeyEvent.wVirtualKeyCode)
            {
            case VK_TAB:
                editorMode = tiles;
                SetTitle();
                break;
            case 'S':
                Save(mapFileName);
                break;
            }
        }
    }
    else if (inputRecord.EventType == MOUSE_EVENT)
    {
        if (mouseY < mapPanelHeight) // ignore clicks on the palette.
        {
            switch (inputRecord.Event.MouseEvent.dwButtonState)
            {
            case FROM_LEFT_1ST_BUTTON_PRESSED:
                walkabilityGrid[mouseY][mouseX] = false;
                break;
            case RIGHTMOST_BUTTON_PRESSED:
                walkabilityGrid[mouseY][mouseX] = true;
                break;
            }
        }
    }
}

void HandleInput()
{
    DWORD numEvents = 0;
    GetNumberOfConsoleInputEvents(stdInput, &numEvents);

    if (numEvents > 0)
    {
        DWORD numEvents = 0;
        INPUT_RECORD inputRecord;
        ReadConsoleInput(stdInput, &inputRecord, 1, &numEvents);

        if (inputRecord.EventType == MOUSE_EVENT)
        {
            COORD mousePosition = inputRecord.Event.MouseEvent.dwMousePosition;
            mouseX = mousePosition.X;
            mouseY = mousePosition.Y;
        }

        switch (editorMode)
        {
        case tiles:
            HandleInputTileMode(inputRecord);
            break;
        case walkability:
            HandleInputWalkabilityMode(inputRecord);
            break;
        }
    }
}

void DrawTileMode()
{
    // copy map data into screen buffer
    for (int y = 0; y < mapPanelHeight; ++y)
    {
        for (int x = 0; x < mapPanelWidth; ++x)
        {
            screenBuffer[y][x] = mapBuffer[y][x];
        }
    }

    // copy mouse selection
    if (hasSelection && mouseY < mapPanelHeight)
    {
        screenBuffer[mouseY][mouseX] = selection;
    }
}

void DrawWalkabilityMode()
{
    for (int y = 0; y < mapPanelHeight; ++y)
    {
        for (int x = 0; x < mapPanelWidth; ++x)
        {
            screenBuffer[y][x].Char.AsciiChar = mapBuffer[y][x].Char.AsciiChar;
            screenBuffer[y][x].Attributes = BACKGROUND_INTENSITY | ((walkabilityGrid[y][x]) ? BACKGROUND_GREEN : BACKGROUND_RED);
        }
    }
}

void Draw()
{
    switch (editorMode)
    {
    case tiles:
        DrawTileMode();
        break;
    case walkability:
        DrawWalkabilityMode();
        break;
    }

    WriteConsoleOutput(stdOutput, (CHAR_INFO*)screenBuffer, screenSize, topLeft, &screenRect);
}

int main()
{
    InitConsole();
    InitWalkabilityGrid();
    InitPalette();
    Load(mapFileName);

    for (;;)
    {
        HandleInput();
        Draw();
    }
}