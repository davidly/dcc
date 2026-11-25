{ BYTE magazine October 1982. Jerry Pournelle. Matrix multiply. }
{ integer version: pint has no real type or multi-dimensional arrays, }
{ so the arrays are indexed by hand and the sum is kept in base 10000 }
{ expected result: 465880 }

program mm;

type
    matrixType = array[ 0..399 ] of integer;

var
    summHi, summLo : integer;
    a : matrixType;
    b : matrixType;
    c : matrixType;

procedure filla;
var
    i, j : integer;
begin
    for i := 0 to 19 do
        for j := 0 to 19 do
            a[ i * 20 + j ] := i + j + 2;
end;

procedure fillb;
var
    i, j : integer;
begin
    for i := 0 to 19 do
        for j := 0 to 19 do
            b[ i * 20 + j ] := ( i + j + 2 ) div ( j + 1 );
end;

procedure fillc;
var
    i, j : integer;
begin
    for i := 0 to 19 do
        for j := 0 to 19 do
            c[ i * 20 + j ] := 0;
end;

procedure matmult;
var
    i, j, k : integer;
begin
    for i := 0 to 19 do
        for j := 0 to 19 do
            for k := 0 to 19 do
                c[ i * 20 + j ] := c[ i * 20 + j ] + a[ i * 20 + k ] * b[ k * 20 + j ];
end;

procedure summit;
var
    i : integer;
begin
    for i := 0 to 399 do
    begin
        summLo := summLo + c[ i ];
        while summLo >= 10000 do
        begin
            summLo := summLo - 10000;
            summHi := summHi + 1;
        end;
    end;
end;

begin
    summHi := 0;
    summLo := 0;

    filla;
    fillb;
    fillc;
    matmult;
    summit;

    write( 'summ is : ', summHi );
    if summLo < 1000 then write( '0' );
    if summLo < 100 then write( '0' );
    if summLo < 10 then write( '0' );
    writeln( summLo );
end.
