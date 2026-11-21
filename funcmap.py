import re,sys
def funcs(path):
    t=open(path).read().splitlines()
    st=[]
    for i,l in enumerate(t):
        if re.search(r'\senter (0x)?[0-9a-f]+, 0$',l): st.append(i)
        elif re.search(r'\spush bp$',l) and i+1<len(t) and 'mov bp, sp' in t[i+1]: st.append(i)
    st.append(len(t))
    out=[]
    for a,b in zip(st,st[1:]):
        out.append((int(t[a].split()[0].split(':')[1],16),a,b))
    return t,out
if __name__=='__main__':
    for seg in sys.argv[1:]:
        t,f=funcs(f'asm/seg_{seg}.asm')
        print(f"seg {seg}: {len(f)} functions (prev. detector undercounted)")
