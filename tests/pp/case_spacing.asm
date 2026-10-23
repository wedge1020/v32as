%DEFINE one 1
  % Define two 2
%IfNDef nothing
	%ifdef ONE
	  mov R0, 100
	%ELSE
	  mov R0, one
	%EndIf
%endif
 mov R1, two
%
% ; an empty directive is legal
