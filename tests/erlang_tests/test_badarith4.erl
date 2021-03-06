-module(test_badarith4).
-export([start/0, id/1, bor2/2]).

start() ->
    bor2(id([]), id(1000)) + band2(id([]), id(0)) + bxor2(id([]), id(500)) +
    bsl2(id([]), id(0)) + bsr2(id([]), id(1)) + bnot1(id(true)).

id(X) ->
    X.

bor2(A, B) ->
    try A bor B of
        Result -> Result
    catch
        error:badarith -> -1;
        _:_ -> -2
    end.

band2(A, B) ->
    try A band B of
        Result -> Result
    catch
        error:badarith -> -4;
        _:_ -> -8
    end.

bxor2(A, B) ->
    try A bxor B of
        Result -> Result
    catch
        error:badarith -> -16;
        _:_ -> -32
    end.

bsl2(A, B) ->
    try A bsl B of
        Result -> Result
    catch
        error:badarith -> -64;
        _:_ -> -128
    end.

bsr2(A, B) ->
    try A bsr B of
        Result -> Result
    catch
        error:badarith -> -256;
        _:_ -> -512
    end.

bnot1(A) ->
    try bnot A of
        Result -> Result
    catch
        error:badarith -> -1024;
        _:_ -> -2048
    end.
