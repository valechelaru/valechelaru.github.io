const unsigned int WIDTH = 600;
const unsigned int HEIGHT = 600;
unsigned int BUFFER[WIDTH * HEIGHT];

void go() {
    unsigned int screen[600][600];
    int x;
    for (x = 0; x < WIDTH; x++) {
        int y;
        for (y = 0; y < HEIGHT; y++) {
            unsigned int color = 0xffff0000; //RGBA little endian - 32bit int is enough 4x8bit
            if (x < WIDTH/2)
            {
                color = 0xff0000ff;
            } else {
                color = 0xffff0000;
            }
            
            screen[x][y] = color;
            BUFFER[WIDTH * x + y] = screen[x][y];
        }
    }
}