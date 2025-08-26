
import array
import mpix

print(dir(mpix))

#buf = bytearray(100)
buf = array.array('B', (0 for n in range(100)))
img = mpix.from_buf(buf, 10, 10)
print(img)
del img


