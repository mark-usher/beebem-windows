# Builds the BeebEm SCSI test program from Pi1MHz's test_beebscsi.c (see run_tests.sh)
import re,sys
import os
src=open(sys.argv[1]).read()
here=sys.argv[2]
out=sys.argv[3]

def func_span(s, name):
    m=re.search(r'\nstatic (void|int|uint32_t|bool|long) '+name+r'\(', s)
    assert m, name
    i=m.start()+1
    j=s.index('{',i); d=0; k=j
    while True:
        if s[k]=='{': d+=1
        elif s[k]=='}':
            d-=1
            if d==0: break
        k+=1
    # include the comment block immediately above (a /* */ block or // lines)
    start=i
    prev=s.rfind('\n',0,start-1); line=s[prev+1:start-1]
    if line.rstrip().endswith('*/'):
        k2=s.rfind('/*',0,start)
        start=s.rfind('\n',0,k2)+1
    else:
        while line.startswith('//'):
            start=prev+1
            prev=s.rfind('\n',0,start-1); line=s[prev+1:start-1]
    return start,k+1

drop=['test_format_after_jukebox','test_format_expand_fails','test_jukebox','test_stop_directory_luns',
      'test_bsfatread_failure','test_fcode_short','test_readonly_image','test_title_cache','test_storage',
      'test_storage_choice','test_storage_choice_smb']
for n in drop:
    a,b=func_span(src,n)
    src=src[:a]+src[b:]

# platform section: everything from includes to the harness section is replaced
i=src.index('#include <stdio.h>')
j=src.index('/* ---- harness ---')
src=src[:i]+open(os.path.join(here,'stubs.inc')).read()+src[j:]

# main: our initialisation and test list
i=src.index('int main(int argc, char **argv)')
src=src[:i]+open(os.path.join(here,'main.inc')).read()
open(out,'w').write(src)
