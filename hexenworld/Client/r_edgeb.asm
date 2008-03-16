;
; r_edgeb.asm
; x86 assembly-language edge-processing code.
;
; this file uses NASM syntax.
; $Id: r_edgeb.asm,v 1.7 2008-03-16 14:30:55 sezero Exp $
;

%include "asm_nasm.inc"

; underscore prefix handling
; for C-shared symbols:
%ifmacro _sym_prefix
; C-shared externs:
 _sym_prefix r_bmodelactive
 _sym_prefix surfaces
 _sym_prefix edge_tail
 _sym_prefix edge_aftertail
 _sym_prefix edge_head
 _sym_prefix edge_head_u_shift20
 _sym_prefix edge_tail_u_shift20
 _sym_prefix current_iv
 _sym_prefix span_p
 _sym_prefix fv
; C-shared globals:
 _sym_prefix R_EdgeCodeStartT
 _sym_prefix R_GenerateTSpans
 _sym_prefix R_EdgeCodeEndT
;_sym_prefix R_SurfacePatchT
%endif	; _sym_prefix

; externs from C code
 extern r_bmodelactive
 extern surfaces
 extern edge_tail
 extern edge_aftertail
 extern edge_head
 extern edge_head_u_shift20
 extern edge_tail_u_shift20
 extern current_iv
 extern span_p
 extern fv

; externs from ASM-only code


SEGMENT .data

Ltemp dd 0
float_1_div_0100000h dd 035800000h
float_point_999 dd 0.999
float_1_point_001 dd 1.001


SEGMENT .text

 ALIGN 4

;;;;;;;;;;;;;;;;;;;;;;;;
; R_EdgeCodeStartT
;;;;;;;;;;;;;;;;;;;;;;;;
 global R_EdgeCodeStartT
R_EdgeCodeStartT:
TrailingEdge:
 mov eax, dword [20+esi]
 dec eax
 jnz LInverted
 mov  dword [20+esi],eax
 mov ecx, dword [40+esi]
 mov edx, dword [12345678h]
LPatch0:
 mov eax, dword [r_bmodelactive]
 sub eax,ecx
 cmp edx,esi
 mov  dword [r_bmodelactive],eax
 jnz LNoEmit
 mov eax, dword [0+ebx]
 shr eax,20
 mov edx, dword [16+esi]
 mov ecx, dword [0+esi]
 cmp eax,edx
 jle LNoEmit2

;rj
 bt  dword [24+esi],7   ; surf->flags & SURF_TRANSLUCENT
 jnc LNoEmit2

 mov  dword [16+ecx],eax
 sub eax,edx
 mov  dword [0+ebp],edx
 mov  dword [8+ebp],eax
 mov eax, dword [current_iv]
 mov  dword [4+ebp],eax
 mov eax, dword [8+esi]
 mov  dword [12+ebp],eax
 mov  dword [8+esi],ebp
 add ebp,16
 mov edx, dword [0+esi]
 mov esi, dword [4+esi]
 mov  dword [0+esi],edx
 mov  dword [4+edx],esi
 ret
LNoEmit2:
 mov  dword [16+ecx],eax
 mov edx, dword [0+esi]
 mov esi, dword [4+esi]
 mov  dword [0+esi],edx
 mov  dword [4+edx],esi
 ret
LNoEmit:
 mov edx, dword [0+esi]
 mov esi, dword [4+esi]
 mov  dword [0+esi],edx
 mov  dword [4+edx],esi
 ret
LInverted:
 mov  dword [20+esi],eax
 ret

Lgs_trailing:
 push offset dword Lgs_nextedge
 jmp TrailingEdge


;;;;;;;;;;;;;;;;;;;;;;;;
; R_GenerateTSpans
;;;;;;;;;;;;;;;;;;;;;;;;

 global R_GenerateTSpans
R_GenerateTSpans:
 push ebp
 push edi
 push esi
 push ebx
 mov eax, dword [surfaces]
 mov edx, dword [edge_head_u_shift20]
 add eax,64
 mov ebp, dword [span_p]
 mov  dword [r_bmodelactive],0
 mov  dword [0+eax],eax
 mov  dword [4+eax],eax
 mov  dword [16+eax],edx
 mov ebx, dword [edge_head+12]
 cmp ebx,offset edge_tail
 jz near Lgs_lastspan
Lgs_edgeloop:
 mov edi, dword [16+ebx]
 mov eax, dword [surfaces]
 mov esi,edi
 and edi,0FFFF0000h
 and esi,0FFFFh
 jz Lgs_leading
 shl esi,6
 add esi,eax
 test edi,edi
 jz Lgs_trailing
 call TrailingEdge			; call near TrailingEdge
 mov eax, dword [surfaces]
Lgs_leading:
 shr edi,16-6
 mov eax, dword [surfaces]
 add edi,eax
 mov esi, dword [12345678h]
LPatch2:
 mov edx, dword [20+edi]
 mov eax, dword [40+edi]
 test eax,eax
 jnz Lbmodel_leading
 test edx,edx
 jnz near Lxl_done
 inc edx
 mov eax, dword [12+edi]
 mov  dword [20+edi],edx
 mov ecx, dword [12+esi]
 cmp eax,ecx
 jl near Lnewtop
Lsortloopnb:
 mov esi, dword [0+esi]
 mov ecx, dword [12+esi]
 cmp eax,ecx
 jge Lsortloopnb
 jmp LInsertAndExit

 ALIGN 4

Lbmodel_leading:
 test edx,edx
 jnz near Lxl_done
 mov ecx, dword [r_bmodelactive]
 inc edx
 inc ecx
 mov  dword [20+edi],edx
 mov  dword [r_bmodelactive],ecx
 mov eax, dword [12+edi]
 mov ecx, dword [12+esi]
 cmp eax,ecx
 jl near Lnewtop
 jz near Lzcheck_for_newtop
Lsortloop:
 mov esi, dword [0+esi]
 mov ecx, dword [12+esi]
 cmp eax,ecx
 jg Lsortloop
 jne near LInsertAndExit
 mov eax, dword [0+ebx]
 sub eax,0FFFFFh
 mov  dword [Ltemp],eax
 fild  dword [Ltemp]
 fmul  dword [float_1_div_0100000h]
 fld st0
 fmul  dword [48+edi]
 fld  dword [fv]
 fmul  dword [52+edi]
 fxch st1
 fadd  dword [44+edi]
 fld  dword [48+esi]
 fmul st0,st3
 fxch st1
 faddp st2,st0
 fld  dword [fv]
 fmul  dword [52+esi]
 fld st2
 fmul  dword [float_point_999]
 fxch st2
 fadd  dword [44+esi]
 faddp st1,st0
 fxch st1
 fcomp st1
 fxch st1
 fmul  dword [float_1_point_001]
 fxch st1
 fnstsw ax
 test ah,001h
 jz Lgotposition_fpop3
 fcomp st1
 fnstsw ax
 test ah,045h
 jz near Lsortloop_fpop2
 fld  dword [48+edi]
 fcomp  dword [48+esi]
 fnstsw ax
 test ah,001h
 jz Lgotposition_fpop2
 fstp st0
 fstp st0
 mov eax, dword [12+edi]
 jmp Lsortloop
Lgotposition_fpop3:
 fstp st0
Lgotposition_fpop2:
 fstp st0
 fstp st0
 jmp LInsertAndExit
Lnewtop_fpop3:
 fstp st0
Lnewtop_fpop2:
 fstp st0
 fstp st0
 mov eax, dword [12+edi]
Lnewtop:
 mov eax, dword [0+ebx]
 mov edx, dword [16+esi]
 shr eax,20
 mov  dword [16+edi],eax
 cmp eax,edx
 jle LInsertAndExit

;rj
 bt  dword [24+esi],7   ; surf->flags & SURF_TRANSLUCENT
 jnc LInsertAndExit

 sub eax,edx
 mov  dword [0+ebp],edx
 mov  dword [8+ebp],eax
 mov eax, dword [current_iv]
 mov  dword [4+ebp],eax
 mov eax, dword [8+esi]
 mov  dword [12+ebp],eax
 mov  dword [8+esi],ebp
 add ebp,16
LInsertAndExit:
 mov  dword [0+edi],esi
 mov eax, dword [4+esi]
 mov  dword [4+edi],eax
 mov  dword [4+esi],edi
 mov  dword [0+eax],edi
Lgs_nextedge:
 mov ebx, dword [12+ebx]
 cmp ebx,offset edge_tail
 jnz near Lgs_edgeloop
Lgs_lastspan:
 mov esi, dword [12345678h]
LPatch3:
 mov eax, dword [edge_tail_u_shift20]
 xor ecx,ecx
 mov edx, dword [16+esi]
 sub eax,edx
 jle Lgs_resetspanstate

;rj
 bt  dword [24+esi],7   ; surf->flags & SURF_TRANSLUCENT
 jnc Lgs_resetspanstate

 mov  dword [0+ebp],edx
 mov  dword [8+ebp],eax
 mov eax, dword [current_iv]
 mov  dword [4+ebp],eax
 mov eax, dword [8+esi]
 mov  dword [12+ebp],eax
 mov  dword [8+esi],ebp
 add ebp,16
Lgs_resetspanstate:
 mov  dword [20+esi],ecx
 mov esi, dword [0+esi]
 cmp esi,012345678h
LPatch4:
 jnz Lgs_resetspanstate
 mov  dword [span_p],ebp
 pop ebx
 pop esi
 pop edi
 pop ebp
 ret

 ALIGN 4

Lxl_done:
 inc edx
 mov  dword [20+edi],edx
 jmp Lgs_nextedge

 ALIGN 4

Lzcheck_for_newtop:
 mov eax, dword [0+ebx]
 sub eax,0FFFFFh
 mov  dword [Ltemp],eax
 fild  dword [Ltemp]
 fmul  dword [float_1_div_0100000h]
 fld st0
 fmul  dword [48+edi]
 fld  dword [fv]
 fmul  dword [52+edi]
 fxch st1
 fadd  dword [44+edi]
 fld  dword [48+esi]
 fmul st0,st3
 fxch st1
 faddp st2,st0
 fld  dword [fv]
 fmul  dword [52+esi]
 fld st2
 fmul  dword [float_point_999]
 fxch st2
 fadd  dword [44+esi]
 faddp st1,st0
 fxch st1
 fcomp st1
 fxch st1
 fmul  dword [float_1_point_001]
 fxch st1
 fnstsw ax
 test ah,001h
 jz near Lnewtop_fpop3
 fcomp st1
 fnstsw ax
 test ah,045h
 jz Lsortloop_fpop2
 fld  dword [48+edi]
 fcomp  dword [48+esi]
 fnstsw ax
 test ah,001h
 jz near Lnewtop_fpop2
Lsortloop_fpop2:
 fstp st0
 fstp st0
 mov eax, dword [12+edi]
 jmp Lsortloop

;;;;;;;;;;;;;;;;;;;;;;;;
; R_EdgeCodeEndT
;;;;;;;;;;;;;;;;;;;;;;;;
 global R_EdgeCodeEndT
R_EdgeCodeEndT:


;;;;;;;;;;;;;;;;;;;;;;;;
; R_SurfacePatchT
;;;;;;;;;;;;;;;;;;;;;;;;

 ALIGN 4

 global R_SurfacePatchT
R_SurfacePatchT:
 mov eax, dword [surfaces]
 add eax,64
 mov  dword [LPatch4-4],eax
 add eax,0
 mov  dword [LPatch0-4],eax
 mov  dword [LPatch2-4],eax
 mov  dword [LPatch3-4],eax
 ret

