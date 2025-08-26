
import array
import mpix

from microbmp import MicroBMP

def load_bmp(path)
    bmp = MicroBMP().load(path)
    width = bmp.DIB_w
    height = bmp.DIB_h
    print('load-bmp', path, width, height)
    return mpix.from_buf(bmp.parray, width, height)


def make_filled(width, height, value=0):
    buf = array.array('B', (value for n in range(width*height)))
    img = mpix.from_buf(buf, width, height)
    return img


def test_black_correct():
    width, height = 10, 10

    inp = make_filled(width, height)
    print('inp', inp)

    # test black-level correction
    inp.correct(10)

    out = array.array('B', (0 for n in range(width*height)))
    inp.to_buffer(out)

    print(out)

    del img

if __name__ == "__main__":
    test_black_correct()


