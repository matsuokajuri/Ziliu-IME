import unittest
from materializer import character_vocabulary_rows,character_tail_rows
class Tests(unittest.TestCase):
    def test_valid(self):character_vocabulary_rows(['<pad>','<unk>','<bos>','中'],2,1);character_tail_rows([3,4],5)
    def test_bos(self):
        with self.assertRaises(ValueError):character_vocabulary_rows(['<pad>','<unk>','<bos>','中'],0,1)
    def test_unk(self):
        with self.assertRaises(ValueError):character_vocabulary_rows(['<pad>','<unk>','<bos>','中'],2,0)
    def test_multichar(self):
        with self.assertRaises(ValueError):character_vocabulary_rows(['<pad>','<unk>','<bos>','中文'],2,1)
    def test_tail_rejects_special(self):
        for i in [0,1,2,True,3.0,5]:
            with self.assertRaises(ValueError):character_tail_rows([i],5)
if __name__=='__main__':unittest.main()
