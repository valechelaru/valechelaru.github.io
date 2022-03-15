const unsigned int WIDTH = 600;
const unsigned int HEIGHT = 600;
unsigned int BUFFER[WIDTH * HEIGHT];

void go() {
    unsigned int screen[HEIGHT][WIDTH];
    int x;
    for (x = 0; x < WIDTH; x++) {
        int y;
        for (y = 0; y < HEIGHT; y++) {
            unsigned int color = 0xffff0000; //RGBA little endian - 32bit int is enough 4x8bit
            if (x < WIDTH/2)
            {
                color = 0xffbc5b00;
            } else {
                color = 0xff00d6ff;
            }
            
            screen[x][y] = color;
            BUFFER[WIDTH * x + y] = screen[x][y];
        }
    }
}