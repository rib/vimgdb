" Prevent multiple loading, allow commenting it out

function Gdb_Interface_Init(pipe_name)
        echo "Can not use vimgdb plugin - your vim must have +signs and +clientserver features"
endfunction


if !(has("clientserver") && has("signs"))
        finish
endif


"default until interface_init
let s:pipe_name = "/dev/null"
let s:Initialised_gdbvim = 0

function Gdb_Not_Initialised_Msg()
        echohl WarningMsg | echomsg "You have not initialised a gdb pipe yet!" | echohl None
endfunction


"concatenate arguments + add \n if more than one argument was supplied
function Gdb_command(cmd, ...)
        if s:Initialised_gdbvim == 0
                call Gdb_Not_Initialised_Msg()
        else
                let i = 1
                let command = a:cmd
                while i <= a:0
                        exe "let command = \"" . command . "\" . \" \" .  a:" . i
                        let i = i + 1
                endwhile
                if i > 1
                        let command = command . "\n"
                endif
                silent exec ":redir >>" . s:pipe_name . "|echon \"" . command . "\"|redir END"
        endif
endfunction
command -nargs=+ -complete=command Gdb call Gdb_command(<f-args>)


" Get ready for communication
function! Gdb_Interface_Init(pipe_name,cwd)
        let s:Initialised_gdbvim = s:Initialised_gdbvim + 1
        
        "if more than one initialisation attempt!
        if s:Initialised_gdbvim > 1
                "send to current gdb session
                call Gdb_command("You can only have one gdb->vim link!\n")

                "send to new gdb session
                let tmp = s:pipe_name
                let s:pipe_name = a:pipe_name
                call Gdb_command("quit\n")
                let s:pipe_name = tmp
        else
                "clear any signs
                sign unplace *

                "define some new signs
                sign define breakpoint linehl=ErrorMsg text=!!
                sign define current linehl=IncSearch text=->

                "this is our communication pipe to gdb
                let s:pipe_name = a:pipe_name

                execute "cd" . a:cwd

                "set up bindings
                call <SID>Gdb_shortcuts()
        endif
endfunction


function Gdb_Interface_Deinit()
        if s:Initialised_gdbvim == 0
                call Gdb_Not_Initialised_Msg()
        else
                let s:Initialised_gdbvim = s:Initialised_gdbvim - 1
                if s:Initialised_gdbvim == 0
                        sign unplace *
                        unmap <F2>
                        unmap <F5>
                        unmap <F6>
                        unmap <F7>
                        unmap <F8>
                        unmap <F9>
                        unmap <F10>
                        nunmenu Gdb
                endif
        endif
endfunction


" Mappings are dependant on Leader at time of loading the macro.
function <SID>Gdb_shortcuts()
        nmap <F9> :call Gdb_command("break " . bufname("%") . ":" . line(".") . "\n")<CR>
        nmap <F10> :call Gdb_command("clear " . bufname("%") . ":" . line(".") . "\n")<CR>
        nmap <F2> :call Gdb_command("run\n")<CR>
        nmap <F5> :call Gdb_command("step\n")<CR>
        nmap <F6> :call Gdb_command("next\n")<CR>
        nmap <F7> :call Gdb_command("finish\n")<CR>
        nmap <F8> :call Gdb_command("continue\n")<CR>
"    vmap <unique> <C-P> "gy:Gdb print <C-R>g<CR>
"    nmap <unique> <C-P> :call Gdb_command("print ".expand("<cword>"))<CR> 
"    nmenu Gdb.Command :<C-U>Gdb 
        nmenu Gdb.Run<Tab><F2> <F2>
        nmenu Gdb.Set\ Breakpoint<Tab><F9> <F9>
        nmenu Gdb.Clear\ Breakpoint<Tab><F10> <F10>
        nmenu Gdb.Step<Tab><F5> <F5>
        nmenu Gdb.Next<Tab><F6> <F6>
        nmenu Gdb.Finish<Tab><F7> <F7>
        nmenu Gdb.Continue<Tab><F8> <F8>
"    nmenu Gdb.Set\ break :call Gdb_command("break ".bufname("%").":".line("."))<CR>
endfunction


function Gdb_Breakpoint(id, file, linenum)
        if s:Initialised_gdbvim == 0
                call Gdb_Not_Initialised_Msg()
        else
                if !bufexists(a:file)
                        execute "bad " . a:file
                endif
                
                execute "sign unplace " . a:id
                execute "sign place " .  a:id ." name=breakpoint line=" . a:linenum . " file=" . a:file
        endif
endfunction


function Gdb_ClearBreakpoint(id)
        if s:Initialised_gdbvim == 0
                call Gdb_Not_Initialised_Msg()
        else
                execute "sign unplace " . a:id
        endif
endfunction


function Gdb_DebugStop(file, line)
        if s:Initialised_gdbvim == 0
                call Gdb_Not_Initialised_Msg()
        else

                if !bufexists(a:file)
                        if !filereadable(a:file)
                                return
                        endif
                        execute "bad ".a:file
                endif
                
                execute "b ".a:file
                let s:file=a:file
                execute "sign unplace " . 300
                execute "sign place " .  300 . " name=current line=" . a:line . " file=" . a:file
                execute a:line
                :silent! foldopen!
        endif
endfunction



