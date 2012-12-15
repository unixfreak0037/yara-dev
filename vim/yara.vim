" Yara syntax file
" Language:	Yara
" Maintainer:	John Davison <unixfreak0037@gmail.com>
" Last Change:	2012 Dec 14

" Quit when a (custom) syntax file was already loaded
if exists("b:current_syntax")
  finish
endif

let s:cpo_save = &cpo
set cpo&vim

" yara keywords
syntax keyword yaraReserved all and any ascii at condition contians entrypoint equals false filesize fullword for global in include index indexes int8 int16 int32 matches meta nocase not or of private rule rva section strings them true uint8 uint16 uint32 wide

" string identifiers
" TODO should only work inside strings and condition section
syntax match yaraStringIdentifier /\$[a-zA-Z0-9_]*/

" comments
" TODO not inside strings
syntax match yaraComment /\/\/.*/

" strings
syntax region yaraString start=/"/ end=/"/ skip=/\\"/

" hex strings
" TODO only inside strings section
syntax region yaraHexString start=/{/ end=/}/

highlight link yaraReserved Statement
highlight link yaraStringIdentifier Identifier
highlight link yaraComment Comment
highlight link yaraString Constant
highlight link yaraHexString Constant

let b:current_syntax = "yara"

let &cpo = s:cpo_save
unlet s:cpo_save
" vim: ts=8
