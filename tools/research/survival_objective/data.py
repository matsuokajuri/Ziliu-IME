"""Only original score constant and file digest; no old dataset paths."""
import hashlib
PENALTY=-13.815510557964274
def sha(path):
 h=hashlib.sha256()
 with open(path,'rb') as f:
  while b:=f.read(1048576):h.update(b)
 return h.hexdigest()
