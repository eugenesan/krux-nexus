data='\x00'*16
data_bytes=''
try:
    data_bytes=data.encode('latin-1')
    print('latin1 ok', len(data_bytes))
except Exception as e:
    print('latin1 failed', e)
if isinstance(data,str) and len(data) in (16,32,48,96):
    data_bytes=bytes([ord(ch)&0xFF for ch in data])
print('len',len(data_bytes))
from embit.bip39 import mnemonic_from_bytes
print(len(mnemonic_from_bytes(data_bytes).split()))
