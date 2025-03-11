

def rc4_ksa(key):
    n = len(key)
    j = 0
    s_box = list(range(256))
    for i in range(256):
        j = (j + s_box[i] + key[i % n]) % 256
        s_box[i], s_box[j] = s_box[j], s_box[i]
    return s_box


def rc4_prga(s_box):
    i = 0
    j = 0
    while True:
        i = (i + 1) % 256
        j = (j + s_box[i]) % 256
        s_box[i], s_box[j] = s_box[j], s_box[i]
        yield s_box[(s_box[i] + s_box[j]) % 256]


def rc4(key):
    return rc4_prga(rc4_ksa(key))
