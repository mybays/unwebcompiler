
(*
注意 :
   LHArc 使用一个 "percolating" 来更新它的 Lempel-Ziv 结构.
   如果你使用 percolating 方法, 压缩器将运行得稍快一些,
   使用更少的内存, 且比标准方法稍 less efficient 一些.
   你可以选择两种方法中的一种, 并应该注意到解压器不受该选择的影响并可以
   解压两种解压器生成的数据.
*)


uses
  SysUtils, Classes;

 // procedure LHACompress(InStr, OutStr: TStream;filename:String);
    (*  LHACompress 从 InStr 的当前位置开始压缩直到的尾部, 放置压缩后的输出 OutStr 的开始到
        OutStr 的当前位置. 如果你需要压缩整个 InStr
        则需要在调用前设置 InStr.Position := 0 .
    *)
  //procedure LHAExpand(InStr, OutStr: TStream);
    (*  LHAExpand starts expanding at InStr's current position and continues to
        the end of InStr, placing the expanded output in OutStr starting at
        OutStr's current position. If you need the entirety of InStr expanded
        you'll need to set InStr.Position := 0 before calling.
    *)


TYPE
{$IFDEF WIN32}
  TwoByteInt  = SmallInt;
{$ELSE}
  TwoByteInt  = Integer;
{$ENDIF}
  PWord=^TWord;
  TWord=ARRAY[0..32759]OF TwoByteInt;
  PByte=^TByte;
  TByte=ARRAY[0..65519]OF Byte;

CONST
(*
注意 :
   以下常数设置 LHArc 使用的常数值.
   你可以修改下面的三个 :
   DICBIT : Lempel-Ziv 字典大小.
   减少该常数会极大地影响压缩效果 !
   但增加它 (仅在 32 位平台中, 如 Delphi 2) 将不会 yield
   noticeably better results.
   如果你设置 DICBIT 到 15 或以上, 设置 PBIT 到 5; 而且如果你设置 DICBIT 到 19
   或以上, 也设置 NPT 为 NP.

   WINBIT : Sliding window 大小.
   压缩率很大程序上取决于该值.
   对于大的文件你可以增加它到 15 来取得更好的结果.
   I recommend doing this if you have enough memory, except if you want that
   your compressed data remain compatible with LHArc.
   On a 32 bit platform, you can increase it to 16. Using a larger value will
   only waste time and memory.

   BUFBIT : I/O Buffer size. You can lower it to save memory, or increase it
   to reduce disk access.
*)

  BITBUFSIZ=16;
  UCHARMAX=255;

  DICBIT=13;
  DICSIZ=1 SHL DICBIT;

  MATCHBIT=8;
  MAXMATCH=1 SHL MATCHBIT;
  THRESHOLD=3;
  PERCFLAG=$8000;

  NC=(UCHARMAX+MAXMATCH+2-THRESHOLD);
  CBIT=9;
  CODEBIT=16;

  NP=DICBIT+1;
  NT=CODEBIT+3;
  PBIT=4; {Log2(NP)}
  TBIT=5; {Log2(NT)}
  NPT=NT; {Greater from NP and NT}
  NUL=0;
  MAXHASHVAL=(3*DICSIZ+(DICSIZ SHR 9+1)*UCHARMAX);

  WINBIT=14;
  WINDOWSIZE=1 SHL WINBIT;

  BUFBIT=13;
  BUFSIZE=1 SHL BUFBIT;

TYPE
  BufferArray = ARRAY[0..PRED(BUFSIZE)]OF Byte;
  LeftRightArray = ARRAY[0..2*(NC-1)]OF Word;
  CTableArray = ARRAY[0..4095]OF Word;
  CLenArray = ARRAY[0..PRED(NC)]OF Byte;
  HeapArray = ARRAY[0..NC]OF Word;

VAR
  OrigSize,CompSize:Longint;
  InFile,OutFile:TStream;

  BitBuf:Word;
  SubBitBuf,BitCount:Word;

  Buffer:^BufferArray;
  BufPtr:Word;

  Left,Right:^LeftRightArray;

  PtTable:ARRAY[0..255]OF Word;
  PtLen:ARRAY[0..PRED(NPT)]OF Byte;
  CTable:^CTableArray;
  CLen:^CLenArray;

  BlockSize:Word;


{********************************** File I/O **********************************}
FUNCTION GETCHECKSUM(STR:STRING):Integer;
Var
  i : Integer;
  b : Integer;
begin
  b := 0;
  For i := 1 to Length(STR) do b := b + ord(STR[i]);
  b := b mod 256;
  GETCHECKSUM := b;
end;

FUNCTION GetC:Byte;
BEGIN
  IF BufPtr=0 THEN InFile.Read(Buffer^,BUFSIZE);
  GetC:=Buffer^[BufPtr];
  BufPtr:=SUCC(BufPtr) AND PRED(BUFSIZE);
END;

PROCEDURE PutC(c:Byte);
BEGIN
  IF BufPtr=BUFSIZE THEN
    BEGIN
      OutFile.Write(Buffer^,BUFSIZE);
      BufPtr:=0;
    END;
  Buffer^[BufPtr]:=C;
  INC(BufPtr);
END;

FUNCTION BRead(p:POINTER;n:TwoByteInt):TwoByteInt;
BEGIN
  BRead := InFile.Read(p^,n);
END;

PROCEDURE BWrite(p:POINTER;n:TwoByteInt);
BEGIN
  OutFile.Write(p^,n);
END;

{**************************** Bit handling routines ***************************}

PROCEDURE FillBuf(n:TwoByteInt);
BEGIN
  BitBuf:=(BitBuf SHL n);
  WHILE n>BitCount DO BEGIN
    DEC(n,BitCount);
    BitBuf:=BitBuf OR (SubBitBuf SHL n);
    IF (CompSize<>0) THEN
      BEGIN
        DEC(CompSize);
        SubBitBuf:=GetC;
      END ELSE
        SubBitBuf:=0;
    BitCount:=8;
  END;
  DEC(BitCount,n);
  BitBuf:=BitBuf OR (SubBitBuf SHR BitCount);
END;

FUNCTION GetBits(n:TwoByteInt):Word;
BEGIN
  GetBits:=BitBuf SHR (BITBUFSIZ-n);
  FillBuf(n);
END;

PROCEDURE PutBits(n:TwoByteInt;x:Word);
BEGIN
  IF n<BitCount THEN
    BEGIN
      DEC(BitCount,n);
      SubBitBuf:=SubBitBuf OR (x SHL BitCount);
    END ELSE BEGIN
      DEC(n,BitCount);
      PutC(SubBitBuf OR (x SHR n));
      INC(CompSize);
      IF n<8 THEN
        BEGIN
          BitCount:=8-n;
          SubBitBuf:=x SHL BitCount;
        END ELSE BEGIN
          PutC(x SHR (n-8));
          INC(CompSize);
          BitCount:=16-n;
          SubBitBuf:=x SHL BitCount;
        END;
    END;
END;

PROCEDURE InitGetBits;
BEGIN
  BitBuf:=0;
  SubBitBuf:=0;
  BitCount:=0;
  FillBuf(BITBUFSIZ);
END;

PROCEDURE InitPutBits;
BEGIN
  BitCount:=8;
  SubBitBuf:=0;
END;

{******************************** Decompression *******************************}

PROCEDURE MakeTable(nchar:TwoByteInt;BitLen:PByte;TableBits:TwoByteInt;Table:PWord);
VAR
  count,weight:ARRAY[1..16]OF Word;
  start:ARRAY[1..17]OF Word;
  p:PWord;
  i,k,Len,ch,jutbits,Avail,nextCode,mask:TwoByteInt;
BEGIN
  FOR i:=1 TO 16 DO
    count[i]:=0;
  FOR i:=0 TO PRED(nchar) DO
    INC(count[BitLen^[i]]);
  start[1]:=0;
  FOR i:=1 TO 16 DO
    start[SUCC(i)]:=start[i]+(count[i] SHL (16-i));
  IF start[17]<>0 THEN
    HALT(1);
  jutbits:=16-TableBits;
  FOR i:=1 TO TableBits DO
    BEGIN
      start[i]:=start[i] SHR jutbits;
      weight[i]:=1 SHL (TableBits-i);
    END;
  i:=SUCC(TableBits);
  WHILE (i<=16) DO BEGIN
    weight[i]:=1 SHL (16-i);
    INC(i);
  END;
  i:=start[SUCC(TableBits)] SHR jutbits;
  IF i<>0 THEN
    BEGIN
      k:=1 SHL TableBits;
      WHILE i<>k DO BEGIN
        Table^[i]:=0;
        INC(i);
      END;
    END;
  Avail:=nchar;
  mask:=1 SHL (15-TableBits);
  FOR ch:=0 TO PRED(nchar) DO
    BEGIN
      Len:=BitLen^[ch];
      IF Len=0 THEN
        CONTINUE;
      k:=start[Len];
      nextCode:=k+weight[Len];
      IF Len<=TableBits THEN
        BEGIN
          FOR i:=k TO PRED(nextCode) DO
            Table^[i]:=ch;
        END ELSE BEGIN
          p:=Addr(Table^[word(k) SHR jutbits]);
          i:=Len-TableBits;
          WHILE i<>0 DO BEGIN
            IF p^[0]=0 THEN
              BEGIN
                right^[Avail]:=0;
                left^[Avail]:=0;
                p^[0]:=Avail;
                INC(Avail);
              END;
            IF (k AND mask)<>0 THEN
              p:=addr(right^[p^[0]])
            ELSE
              p:=addr(left^[p^[0]]);
            k:=k SHL 1;
            DEC(i);
          END;
          p^[0]:=ch;
        END;
      start[Len]:=nextCode;
    END;
END;

PROCEDURE ReadPtLen(nn,nBit,ispecial:TwoByteInt);
VAR
  i,c,n:TwoByteInt;
  mask:Word;
BEGIN
  n:=GetBits(nBit);
  IF n=0 THEN
    BEGIN
      c:=GetBits(nBit);
      FOR i:=0 TO PRED(nn) DO
        PtLen[i]:=0;
      FOR i:=0 TO 255 DO
        PtTable[i]:=c;
    END ELSE BEGIN
      i:=0;
      WHILE (i<n) DO BEGIN
        c:=BitBuf SHR (BITBUFSIZ-3);
        IF c=7 THEN
          BEGIN
            mask:=1 SHL (BITBUFSIZ-4);
            WHILE (mask AND BitBuf)<>0 DO BEGIN
              mask:=mask SHR 1;
              INC(c);
            END;
          END;
        IF c<7 THEN
          FillBuf(3)
        ELSE
          FillBuf(c-3);
        PtLen[i]:=c;
        INC(i);
        IF i=ispecial THEN
          BEGIN
            c:=PRED(TwoByteInt(GetBits(2)));
            WHILE c>=0 DO BEGIN
              PtLen[i]:=0;
              INC(i);
              DEC(c);
            END;
          END;
      END;
      WHILE i<nn DO BEGIN
        PtLen[i]:=0;
        INC(i);
      END;
      MakeTable(nn,@PtLen,8,@PtTable);
    END;
END;

PROCEDURE ReadCLen;
VAR
  i,c,n:TwoByteInt;
  mask:Word;
BEGIN
  n:=GetBits(CBIT);
  IF n=0 THEN
    BEGIN
      c:=GetBits(CBIT);
      FOR i:=0 TO PRED(NC) DO
        CLen^[i]:=0;
      FOR i:=0 TO 4095 DO
        CTable^[i]:=c;
    END ELSE BEGIN
      i:=0;
      WHILE i<n DO BEGIN
        c:=PtTable[BitBuf SHR (BITBUFSIZ-8)];
        IF c>=NT THEN
          BEGIN
            mask:=1 SHL (BITBUFSIZ-9);
            REPEAT
              IF (BitBuf AND mask)<>0 THEN
                c:=right^[c]
              ELSE
                c:=left^[c];
              mask:=mask SHR 1;
            UNTIL c<NT;
          END;
        FillBuf(PtLen[c]);
        IF c<=2 THEN
          BEGIN
            IF c=1 THEN
              c:=2+GetBits(4)
            ELSE
              IF c=2 THEN
                c:=19+GetBits(CBIT);
            WHILE c>=0 DO BEGIN
              CLen^[i]:=0;
              INC(i);
              DEC(c);
            END;
          END ELSE BEGIN
            CLen^[i]:=c-2;
            INC(i);
          END;
      END;
      WHILE i<NC DO BEGIN
        CLen^[i]:=0;
        INC(i);
      END;
      MakeTable(NC,PByte(CLen),12,PWord(CTable));
    END;
END;

FUNCTION DecodeC:Word;
VAR
  j,mask:Word;
BEGIN
  IF BlockSize=0 THEN
    BEGIN
      BlockSize:=GetBits(16);
      ReadPtLen(NT,TBIT,3);
      ReadCLen;
      ReadPtLen(NP,PBIT,-1);
    END;
  DEC(BlockSize);
  j:=CTable^[BitBuf SHR (BITBUFSIZ-12)];
  IF j>=NC THEN
    BEGIN
      mask:=1 SHL (BITBUFSIZ-13);
      REPEAT
        IF (BitBuf AND mask)<>0 THEN
          j:=right^[j]
        ELSE
          j:=left^[j];
        mask:=mask SHR 1;
      UNTIL j<NC;
    END;
  FillBuf(CLen^[j]);
  DecodeC:=j;
END;

FUNCTION DecodeP:Word;
VAR
  j,mask:Word;
BEGIN
  j:=PtTable[BitBuf SHR (BITBUFSIZ-8)];
  IF j>=NP THEN
    BEGIN
      mask:=1 SHL (BITBUFSIZ-9);
      REPEAT
        IF (BitBuf AND mask)<>0 THEN
          j:=right^[j]
        ELSE
          j:=left^[j];
        mask:=mask SHR 1;
      UNTIL j<NP;
    END;
  FillBuf(PtLen[j]);
  IF j<>0 THEN
    BEGIN
      DEC(j);
      j:=(1 SHL j)+GetBits(j);
    END;
  DecodeP:=j;
END;

{declared as static vars}
VAR
  decode_i:Word;
  decode_j:TwoByteInt;

PROCEDURE DecodeBuffer(count:Word;Buffer:PByte);
VAR
  c,r:Word;
BEGIN
  r:=0;
  DEC(decode_j);
  WHILE (decode_j>=0) DO BEGIN
    Buffer^[r]:=Buffer^[decode_i];
    decode_i:=SUCC(decode_i) AND PRED(DICSIZ);
    INC(r);
    IF r=count THEN
      EXIT;
    DEC(decode_j);
  END;
  WHILE TRUE DO BEGIN
    c:=DecodeC;
    IF c<=UCHARMAX THEN
      BEGIN
        Buffer^[r]:=c;
        INC(r);
        IF r=count THEN
          EXIT;
      END ELSE BEGIN
        decode_j:=c-(UCHARMAX+1-THRESHOLD);
        decode_i:=(LongInt(r)-DecodeP-1)AND PRED(DICSIZ);
        DEC(decode_j);
        WHILE decode_j>=0 DO BEGIN
          Buffer^[r]:=Buffer^[decode_i];
          decode_i:=SUCC(decode_i) AND PRED(DICSIZ);
          INC(r);
          IF r=count THEN
            EXIT;
          DEC(decode_j);
        END;
      END;
  END;
END;

PROCEDURE Decode;
VAR
  p:PByte;
  l:Longint;
  a:Word;
BEGIN
  {Initialize decoder variables}
  GetMem(p,DICSIZ);
  InitGetBits;
  BlockSize:=0;
  decode_j:=0;
  {skip file size}
  l:=OrigSize;
  DEC(compSize,4);
  {unpacks the file}
  WHILE l>0 DO BEGIN
    IF l>DICSIZ THEN
      a:=DICSIZ
    ELSE
      a:=l;
    DecodeBuffer(a,p);
    OutFile.Write(p^,a);
    DEC(l,a);
  END;
  FreeMem(p,DICSIZ);
END;




procedure FreeMemory;
begin
  if CLen <> nil    then Dispose(CLen);
     CLen := nil;
  if CTable <> nil  then Dispose(CTable);
     CTable := nil;
  if Right <> nil   then Dispose(Right);
     Right := nil;
  if Left <> nil    then Dispose(Left);
     Left := nil;
  if Buffer <> nil  then Dispose(Buffer);
     Buffer := nil;
end;



procedure InitMemory;
begin
  {In should be harmless to call FreeMemory here, since it won't free
   unallocated memory (i.e., nil pointers).
   So let's call it in case an exception was thrown at some point and
   memory wasn't entirely freed.}
  FreeMemory;
  New(Buffer);
  New(Left);
  New(Right);
  New(CTable);
  New(CLen);
  FillChar(Buffer^,SizeOf(Buffer^),0);
  FillChar(Left^,SizeOf(Left^),0);
  FillChar(Right^,SizeOf(Right^),0);
  FillChar(CTable^,SizeOf(CTable^),0);
  FillChar(CLen^,SizeOf(CLen^),0);

  decode_i := 0;
  BitBuf := 0;
  SubBitBuf := 0;
  BitCount := 0;
  BufPtr := 0;
  FillChar(PtTable, SizeOf(PtTable),0);
  FillChar(PtLen, SizeOf(PtLen),0);
  BlockSize := 0;

end;




procedure LHAExpand(InStr, OutStr: TStream);
begin
 InitMemory;
    InFile := InStr;
    OutFile := OutStr;
    CompSize := InFile.Size - InFile.Position;
    InFile.Read(OrigSize,4);
    Decode;
end;

var
	InString,OutString:TFileStream;
//out:String;
	inf:String;
	otf:String;
begin
inf:=Paramstr(1);
otf:=Paramstr(2);
InString:=TFileStream.Create(inf,fmOpenRead);
OutString:=TFileStream.Create(otf,fmCreate);
LHAExpand(InString,OutString);
end.
