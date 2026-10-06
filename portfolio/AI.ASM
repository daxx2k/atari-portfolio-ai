; Atari Portfolio serial text client. NASM, original 8086, DOS .COM.
; INT 14h initializes serial; raw 82C50 polling transfers data without CTS/DSR.
; UART base comes from the Atari BIOS data area, physical address 0400h.
; Requests: printable ASCII plus LF. Replies: ASCII terminated by EOT (04h).
bits 16
cpu 8086
org 100h

start:
    cld
    push cs
    pop ds
    push cs
    pop es
    xor ax,ax
    int 61h                   ; initialize extended BIOS once before its services
    mov ax,0e00h
    int 61h
    mov [old_mode],dl
    mov ax,0e01h
    mov dl,1
    int 61h                   ; native 40 x 8 text mode
    int 11h
    and ax,0e00h             ; BIOS equipment word serial port count
    jz no_port
    xor dx,dx
    mov ax,00e3h             ; 9600 baud, 8 data bits, no parity, 1 stop
    int 14h
    push es
    xor ax,ax
    mov es,ax
    mov ax,[es:0400h]        ; Atari-documented dynamic UART address
    pop es
    or ax,ax
    jz no_port
    cmp ax,0ffffh
    je no_port
    mov [uart_base],ax
    mov dx,ax
    inc dx                  ; interrupt enable register
    in al,dx
    mov [old_ier],al
    xor al,al
    out dx,al               ; poll RX ourselves; no serial interrupt handler
    add dx,3                ; modem control register
    in al,dx
    mov [old_mcr],al
    and al,0efh             ; disable internal loopback
    or al,3                 ; assert DTR and RTS
    out dx,al
    mov byte [uart_ready],1
    call clear
    mov si,title
    call print
    mov byte [startup_setup],1
    call setup_load
    jc setup_send
    mov byte [startup_setup],0

new_prompt:
    call clear
    mov si,title
    call print
    mov si,prompt_help
    call print
    call volume_widget
    xor cx,cx
    mov di,input_buffer
input_key:
    xor ah,ah
    int 16h                  ; blocking keyboard while composing
    call volume_key
    jnc .ordinary
    call volume_apply
    call volume_widget
    jmp input_key
.ordinary:
    cmp al,27
    je exit
    cmp al,13
    je send_request
    cmp al,8
    je backspace
    cmp al,32
    jb input_key
    cmp al,126
    ja input_key
    cmp cx,160
    jae input_key
    stosb
    inc cx
    call char_out
    jmp input_key
backspace:
    jcxz input_key
    dec cx
    dec di
    push cx
    push di
    mov ah,3
    xor bh,bh
    int 10h
    or dl,dl
    jnz .same_row
    dec dh
    mov dl,40
.same_row:
    dec dl
    mov ah,2
    int 10h
    push dx
    mov al,32
    call char_out
    pop dx
    mov ah,2
    int 10h
    pop di
    pop cx
    jmp input_key
send_request:
    jcxz input_key
    mov byte [di],0
    push cx
    push di
    mov si,input_buffer
    mov di,setup_command
    mov cx,8
    repe cmpsb
    pop di
    pop cx
    jne .normal
    call setup_load
    jc setup_send
    jmp new_prompt
.normal:
    push cx
    push di
    mov si,input_buffer
    mov di,radio_command
    mov cx,7
    repe cmpsb
    pop di
    pop cx
    je radio_start
    push cx
    push di
    mov si,input_buffer
    mov di,editor_command
    mov cx,8
    repe cmpsb
    pop di
    pop cx
    je editor_start
    push si
    push di
    push cx
    mov si,input_buffer
    mov di,camera_command
    mov cx,5
    repe cmpsb
    pop cx
    pop di
    pop si
    je camera_preview
    push si
    push di
    push cx
    mov si,input_buffer
    mov di,live_command
    mov cx,6
    repe cmpsb
    pop cx
    pop di
    pop si
    je live_preview
    push si
    push di
    push cx
    mov si,input_buffer
    mov di,talk_command
    mov cx,6
    repe cmpsb
    pop cx
    pop di
    pop si
    je talk_preview
    call clear
    mov si,waiting
    call print
    mov si,input_buffer
.send:
    lodsb
    or al,al
    jz .lf
    call serial_send
    jc tx_error
    jmp .send
.lf:
    mov al,10
    call serial_send
    jc tx_error
receive_begin:
    mov byte [text_frame_started],0
    mov di,response_buffer
    mov word [response_length],0
    mov byte [overflow],0
    mov byte [stream_started],0
    mov byte [stream_row],0
    mov byte [stream_col],0
    mov byte [stream_wrapped],0
    mov ah,2
    int 1ah                  ; RTC seconds: portable receive deadline
    mov [last_second],dh
    mov word [seconds_left],180
    mov cx,4096
receive:
.status:
    mov dx,[uart_base]
    add dx,5
    in al,dx                ; LSR; do not wait for DSR or CTS
    test al,1eh             ; overrun, parity, framing, break
    jnz rx_error
    test al,1
    jz .idle
    sub dx,5
    in al,dx
    mov cx,4096
    cmp al,2
    jne .framed
    mov byte [text_frame_started],1
    jmp receive
.framed:
    cmp byte [text_frame_started],0
    je receive
    cmp al,4
    je show_response
    cmp al,13
    je receive
    cmp al,10
    je .store
    cmp al,32
    jb receive
    cmp al,126
    ja receive
.store:
    cmp word [response_length],4095
    jae .full
    stosb
    cmp byte [startup_setup],0
    jne .stored
    cmp byte [radio_mode],0
    jne .stored
    call stream_preview_char
.stored:
    inc word [response_length]
    jmp receive
.full:
    mov byte [overflow],1
    jmp receive              ; consume whole frame, retain a bounded reply
.idle:
    loop receive
    ; Slow BIOS calls run only after a sustained idle interval, not per byte.
    mov cx,4096
    cmp byte [radio_mode],0
    jne .deadline             ; leave radio keys queued until the status frame ends
    mov ah,1
    int 16h
    jz .deadline
    xor ah,ah
    int 16h
    cmp al,27
    je exit                  ; do not start another request over pending reply
    call volume_key          ; queued; apply only after this response is drained
.deadline:
    mov ah,2
    int 1ah
    cmp dh,[last_second]
    je receive
    mov [last_second],dh
    dec word [seconds_left]
    jnz receive
    mov si,timeout_message
    jmp fatal
show_response:
    mov byte [di],0
    call volume_apply
    cmp byte [radio_mode],0
    jne radio_show
    cmp byte [startup_setup],0
    je .normal
    mov byte [startup_setup],0
    mov si,response_buffer
    mov di,setup_success
    mov cx,19
    repe cmpsb
    je new_prompt
.normal:
    call persist_command_option
    mov si,response_buffer
    mov byte [page_wrapped],0
next_page:
    call clear
    xor bx,bx                ; BL column, BH line, max 6 lines per page
.next:
    lodsb
    or al,al
    jz .finished
    cmp al,10
    je .newline
    mov byte [page_wrapped],0
    call char_out
    inc bl
    cmp bl,40
    jb .next
    xor bl,bl                ; BIOS wraps after col 39
    mov byte [page_wrapped],1
    inc bh
    jmp .check
.newline:
    cmp byte [page_wrapped],0
    je .line_break
    mov byte [page_wrapped],0
    jmp .next
.line_break:
    push si
    mov si,crlf
    call print
    pop si
    xor bl,bl
    inc bh
.check:
    cmp byte [page_wrapped],0
    je .page_limit
    cmp byte [si],10
    jne .page_limit
    inc si
    mov byte [page_wrapped],0
.page_limit:
    cmp bh,6
    jb .next
    cmp byte [si],0
    je .finished
    mov di,more_help
    call page_help
    call volume_page_key
    cmp al,27
    je new_prompt
    jmp next_page
.finished:
    mov di,done_help
    cmp byte [overflow],0
    je .help
    mov di,truncated_help
.help:
    call page_help
    call volume_page_key
    cmp al,27
    je exit
    jmp new_prompt

; /cam is handled locally, using pull frames rather than an endless UART stream.
talk_preview:
    mov byte [talk_mode],1
    mov word [preview_request],talk_request
    jmp preview_begin
live_preview:
    mov byte [talk_mode],0
    mov word [preview_request],live_request
    jmp preview_begin
camera_preview:
    mov byte [talk_mode],0
    mov word [preview_request],camera_request
preview_begin:
    mov byte [camera_lowres],0
    mov byte [camera_exit_pending],0
    mov byte [camera_graphics],0
    push ds
    pop es
    mov di,camera_previous
    mov cx,1920
    xor ax,ax
    rep stosb
.next_frame:
    call volume_apply
    mov si,[preview_request]
.send:
    lodsb
    or al,al
    jz .header
    call serial_send
    jc tx_error
    jmp .send
.header:
    call camera_deadline
    call camera_sync_header
    jc camera_receive_error
    mov di,camera_header+4
    mov cx,8
.header_byte:
    call camera_read_byte
    jc camera_receive_error
    stosb
    loop .header_byte
    cmp byte [talk_mode],0
    je .camera_header
    cmp word [camera_header],5450h ; PT
    jne camera_format_error
    cmp word [camera_header+2],314bh ; K1
    jne camera_format_error
    cmp word [camera_header+4],6
    ja camera_format_error
    mov word [preview_request],talk_poll_request
    cmp word [camera_header+8],0
    jne .full_talk_frame
    cmp word [camera_header+10],0ffffh
    jne camera_checksum_error
    mov si,camera_previous
    mov di,response_buffer
    mov cx,1920
    rep movsb
    jmp .frame_ready
.full_talk_frame:
    cmp word [camera_header+8],1920
    jne camera_format_error
    jmp .body_start
.camera_header:
    mov byte [camera_lowres],0
    cmp word [camera_header],4650h ; PF
    jne camera_format_error
    cmp word [camera_header+2],3143h ; C1
    jne .lowres_header
    cmp word [camera_header+4],240
    jne camera_format_error
    cmp word [camera_header+6],64
    jne camera_format_error
    cmp word [camera_header+8],1920
    jne camera_format_error
    jmp .body_start
.lowres_header:
    cmp word [camera_header+2],314ch ; L1
    jne camera_format_error
    cmp word [camera_header+4],120
    jne camera_format_error
    cmp word [camera_header+6],32
    jne camera_format_error
    cmp word [camera_header+8],480
    jne camera_format_error
    mov byte [camera_lowres],1
.body_start:
    mov di,response_buffer
    mov cx,[camera_header+8]
.body_byte:
    call camera_read_byte
    jc camera_receive_error
    stosb
    loop .body_byte
    ; Capture first. CRC work must not compete with the non-FIFO UART.
    mov si,response_buffer
    mov cx,[camera_header+8]
    mov bx,0ffffh
.crc_byte:
    lodsb
    xor bl,al
    push cx
    mov cx,8
.crc:
    shr bx,1
    jnc .crc_next
    xor bx,0a001h
.crc_next:
    loop .crc
    pop cx
    loop .crc_byte
    cmp bx,[camera_header+10]
    jne camera_checksum_error
    cmp byte [camera_lowres],0
    je .frame_ready
    call camera_expand
.frame_ready:
    cmp byte [camera_exit_pending],0
    jne camera_return
    call volume_apply
    cmp byte [camera_graphics],0
    jne .draw
    mov ax,000ah             ; Atari documented 240x64 monochrome mode
    int 10h
    mov byte [camera_graphics],1
.draw:
    mov si,response_buffer
    mov di,camera_previous
    xor dx,dx               ; Y coordinate
.row:
    xor cx,cx               ; X coordinate
.byte:
    lodsb
    mov bl,al
    xor bl,[di]             ; only changed pixels need BIOS writes
    mov [di],al
    inc di
    mov bh,al
    push si
    mov si,8
.pixel:
    shl bl,1
    jnc .unchanged
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    xor al,al
    test bh,80h
    jz .white
    inc al
.white:
    xor bh,bh
    mov ah,0ch
    int 10h
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.unchanged:
    shl bh,1
    inc cx
    dec si
    jnz .pixel
    pop si
    cmp cx,240
    jb .byte
    call camera_poll_escape
    cmp byte [camera_exit_pending],0
    jne camera_return
    inc dx
    cmp dx,64
    jb .row
    cmp byte [talk_mode],0
    je .next_frame
    call talk_wait
    call volume_apply
    cmp byte [camera_exit_pending],0
    jne camera_return
    cmp word [camera_header+4],0
    je camera_return
    jmp .next_frame

; Expand the 120x32 packet into the existing 240x64 framebuffer. Work backwards
; so the compact input and expanded output can safely share the same buffer.
camera_expand:
    mov si,response_buffer+479
    mov di,response_buffer+1919
    mov bx,camera_double_nibble
    mov cx,32
.row:
    push cx
    mov cx,15
.byte:
    mov dl,[si]
    dec si
    mov al,dl
    and al,0fh
    xlat
    mov [di],al
    mov [di-30],al
    dec di
    mov al,dl
    shr al,1
    shr al,1
    shr al,1
    shr al,1
    xlat
    mov [di],al
    mov [di-30],al
    dec di
    loop .byte
    sub di,30
    pop cx
    loop .row
    ret
camera_double_nibble db 0,3,12,15,48,51,60,63,192,195,204,207,240,243,252,255

camera_deadline:
    push ax
    push dx
    mov ah,2
    int 1ah
    mov [last_second],dh
    mov word [seconds_left],15
    pop dx
    pop ax
    ret

camera_poll_escape:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ah,1
    int 16h
    jz .done
    xor ah,ah
    int 16h
    cmp al,27
    je .escape
    call volume_key
    jmp .done
.escape:
    mov byte [camera_exit_pending],1
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Discard unfinished text or image bytes from a previous aborted request.
; A recognized magic is followed by the existing strict length/CRC checks.
camera_sync_header:
    push bx
    mov bx,4096
    mov word [camera_header],0
    mov word [camera_header+2],0
.scan:
    call camera_read_byte
    jc .error
    push ax
    mov al,[camera_header+1]
    mov [camera_header],al
    mov al,[camera_header+2]
    mov [camera_header+1],al
    mov al,[camera_header+3]
    mov [camera_header+2],al
    pop ax
    mov [camera_header+3],al
    cmp word [camera_header],4650h ; PF
    jne .talk
    cmp word [camera_header+2],3143h ; C1
    je .ready
    cmp word [camera_header+2],314ch ; L1
    je .ready
    cmp word [camera_header+2],3145h ; E1
    je .ready
    jmp .next
.talk:
    cmp word [camera_header],5450h ; PT
    jne .next
    cmp word [camera_header+2],314bh ; K1
    je .ready
.next:
    dec bx
    jnz .scan
.error:
    stc
    jmp .return
.ready:
    clc
.return:
    pop bx
    ret

camera_read_byte:
    push bx
    push cx
    push dx
    mov cx,4096
.poll:
    mov dx,[uart_base]
    add dx,5
    in al,dx
    test al,1eh
    jnz .error
    test al,1
    jnz .ready
    ; BIOS keyboard/clock calls can outlast a serial character. Only call
    ; them after a sustained idle interval, not in every inter-byte gap.
    loop .poll
    call camera_poll_escape
    mov ah,2
    int 1ah
    cmp dh,[last_second]
    je .idle_done
    mov [last_second],dh
    dec word [seconds_left]
    jnz .idle_done
.error:
    stc
    jmp .return
.idle_done:
    mov cx,4096
    jmp .poll
.ready:
    sub dx,5
    in al,dx
    clc
.return:
    pop dx
    pop cx
    pop bx
    ret

camera_restore_text:
    cmp byte [camera_graphics],0
    je .done
    mov ax,0007h
    int 10h
    mov ax,0e01h
    mov dl,1
    int 61h
    mov byte [camera_graphics],0
.done:
    ret
camera_return:
    call talk_stop
    call camera_restore_text
    jmp new_prompt
camera_receive_error:
    call camera_restore_text
    mov si,camera_timeout_message
    jmp fatal
camera_format_error:
    call camera_restore_text
    cmp byte [talk_mode],0
    je .camera
    mov si,talk_format_message
    jmp fatal
.camera:
    mov si,camera_format_message
    jmp fatal
camera_checksum_error:
    call camera_restore_text
    mov si,camera_crc_message
    jmp fatal


talk_wait:
    ; The Portfolio's default timer tick is one second, unlike a PC's 18.2 Hz.
    ; Use the documented RTC seconds instead of assuming a PC tick frequency.
    push ax
    push dx
    mov ah,2
    int 1ah
    mov [talk_second],dh
.wait:
    call camera_poll_escape
    cmp byte [camera_exit_pending],0
    jne .done
    mov ah,2
    int 1ah
    call volume_apply
    cmp dh,[talk_second]
    je .wait
.done:
    pop dx
    pop ax
    ret

talk_stop:
    cmp byte [talk_mode],0
    je .done
    mov byte [talk_mode],0
    mov si,talk_stop_request
.send:
    lodsb
    or al,al
    jz .header
    call serial_send
    jc .done
    jmp .send
.header:
    call camera_deadline
    mov di,camera_header
    mov cx,12
.read:
    call camera_read_byte
    jc .done
    stosb
    loop .read
    cmp word [camera_header],5450h
    jne .done
    cmp word [camera_header+2],314bh
    jne .done
    mov cx,[camera_header+8]
    jcxz .done
    cmp cx,1920
    jne .done
.drain:
    call camera_read_byte
    jc .done
    loop .drain
.done:
    ret

talk_command db '/talk',0
talk_request db '/talk',10,0
talk_poll_request db '/talkpoll',10,0
talk_stop_request db '/talkstop',10,0
preview_request dw camera_request
talk_mode db 0
talk_second db 0

camera_lowres db 0
live_request db '/liveframe',10,0
live_command db '/live',0
camera_request db '/frame',10,0
camera_command db '/cam',0
camera_exit_pending db 0
camera_graphics db 0
camera_header times 12 db 0
camera_previous times 1920 db 0
camera_timeout_message db 'Preview receive error or timeout.',13,10,'Check ESP32 and restart AI.',13,10,'Press any key to exit.',0
camera_format_message db 'Camera unavailable or invalid frame.',13,10,'Use /status after restarting AI.',13,10,'Press any key to exit.',0
talk_format_message db 'Conversation has an invalid frame.',13,10,'Check camera, audio and serial link.',13,10,'Check ESP32 and restart AI.',13,10,'Press any key to exit.',0
camera_crc_message db 'Preview frame checksum failed.',13,10,'Check serial wiring and restart AI.',13,10,'Press any key to exit.',0


; Show the first screen as bytes arrive; keep receiving beyond it for pagination.
stream_preview_char:
    cmp byte [stream_row],6
    jae .done
    cmp byte [stream_started],0
    jne .character
    call clear
    mov byte [stream_started],1
.character:
    cmp al,10
    je .newline
    mov byte [stream_wrapped],0
    call char_out
    inc byte [stream_col]
    cmp byte [stream_col],40
    jb .done
    mov byte [stream_col],0
    mov byte [stream_wrapped],1
    inc byte [stream_row]
    ret
.newline:
    cmp byte [stream_wrapped],0
    je .line_break
    mov byte [stream_wrapped],0
    ret
.line_break:
    push ax
    mov al,13
    call char_out
    mov al,10
    call char_out
    pop ax
    mov byte [stream_col],0
    inc byte [stream_row]
.done:
    ret

serial_send:
    push si
    push cx
    push di
    push bx
    push dx
    mov bl,al
    mov dx,[uart_base]
    add dx,5
    mov cx,0ffffh
.wait:
    in al,dx
    test al,20h             ; transmitter holding register empty
    jnz .ready
    loop .wait              ; finite timeout if the UART does not respond
    stc
    jmp .return
.ready:
    sub dx,5
    mov al,bl
    out dx,al
    cmp byte [setup_crc_active],0
    je .sent
    push ax
    mov ax,[setup_crc]
    xor al,bl
    mov cx,8
.crc:
    shr ax,1
    jnc .crc_next
    xor ax,0a001h
.crc_next:
    loop .crc
    mov [setup_crc],ax
    pop ax
.sent:
    clc
.return:
    pop dx
    pop bx
    pop di
    pop cx
    pop si
    ret
char_out:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ah,0eh
    xor bh,bh
    int 10h
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
print:
    lodsb
    or al,al
    jz .done
    call char_out
    jmp print
.done:
    ret
clear:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ax,0600h
    mov bh,7
    xor cx,cx
    mov dx,0727h
    int 10h
    mov ah,2
    xor bx,bx
    xor dx,dx
    int 10h
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
page_help:
    push si
    mov ah,2
    xor bx,bx
    mov dx,0700h
    int 10h
    mov si,di
    call print
    pop si
    ret
no_port:
    mov si,no_port_message
    jmp fatal
tx_error:
    mov si,tx_message
    jmp fatal
rx_error:
    mov si,rx_message
fatal:
    call clear
    call print
    xor ah,ah
    int 16h
exit:
    cmp byte [uart_ready],0
    je .display
    mov dx,[uart_base]
    add dx,4
    mov al,[old_mcr]
    out dx,al
    sub dx,3
    mov al,[old_ier]
    out dx,al
.display:
    mov ax,0e01h
    mov dl,[old_mode]
    int 61h
    mov ax,4c00h
    int 21h

uart_base dw 0
uart_ready db 0
old_ier db 0
old_mcr db 0
old_mode db 0
last_second db 0
seconds_left dw 180
response_length dw 0
overflow db 0
stream_started db 0
stream_row db 0
stream_col db 0
stream_wrapped db 0
page_wrapped db 0
title db '        S K Y N E T',13,10,'     GLOBAL DEFENSE NETWORK',13,10,'  ------------------------------------',13,10,'  FIELD TERMINAL // v0.4.14',13,10,13,10,0
prompt_help db 'Enter: send  Esc: exit  /help: commands',13,10,13,10,'OPERATOR> ',0
waiting db 'SKYNET UPLINK... Esc: exit',13,10,0
crlf db 13,10,0
more_help db 'Any key: next page  Esc: skip',0
done_help db 'Any key: new question  Esc: exit',0
truncated_help db 'Text truncated. Any key: new question',0
no_port_message db 'Serial interface not detected.',13,10,'Connect it and restart AI.',13,10,'Press any key to exit.',0
tx_message db 'Serial transmission error.',13,10,'UART transmitter not ready.',13,10,'Press any key to exit.',0
rx_message db 'Serial receive error.',13,10,'Press any key to exit.',0
timeout_message db 'No reply within 180 seconds.',13,10,'Check ESP32 and the serial link.',13,10,'Press any key to exit.',0
input_buffer times 161 db 0
response_buffer times 4096 db 0


; A: file is read and validated before transmitting any credentials.
; A missing file preserves previously saved ESP32 settings.
setup_load:
    mov dx,setup_filename
    mov ax,3d00h
    int 21h
    jc .missing
    mov bx,ax
    mov cx,2049
    mov dx,setup_buffer
    mov ah,3fh
    int 21h
    pushf
    push ax
    mov ah,3eh
    int 21h
    pop ax
    popf
    jc .invalid
    cmp ax,2048
    ja .invalid
    or ax,ax
    jz .invalid
    mov [setup_length],ax
    mov cx,ax
    mov si,setup_buffer
    xor bx,bx
.validate:
    lodsb
    cmp al,26
    je .eof
    cmp al,10
    je .newline
    cmp al,13
    je .next
    cmp al,32
    jb .invalid
    cmp al,126
    ja .invalid
    inc bx
    cmp bx,590
    ja .invalid
.next:
    loop .validate
    stc
    ret
.newline:
    xor bx,bx
    jmp .next
.eof:
    mov ax,si
    sub ax,setup_buffer+1
    mov [setup_length],ax
    stc
    ret
.missing:
    mov word [setup_length],0
    cmp ax,2
    je .absent
    cmp ax,3
    je .absent
.invalid:
    cmp byte [quiet_setup],0
    jne .absent
    call clear
    mov si,setup_bad_message
    call print
    xor ah,ah
    int 16h
.absent:
    clc
    ret

setup_send:
    cmp byte [startup_setup],0
    jne .transfer
    call clear
    mov si,setup_loading_message
    call print
 .transfer:
    mov si,setup_begin
    call setup_send_string
    mov word [setup_crc],0ffffh
    mov byte [setup_crc_active],1
    mov si,setup_buffer
    mov bp,[setup_length]
.line:
    push si
    mov si,setup_prefix
    call setup_send_string
    pop si
.byte:
    or bp,bp
    jz .last
    lodsb
    dec bp
    cmp al,13
    je .byte
    cmp al,10
    je .newline
    call serial_send
    jc tx_error
    jmp .byte
.newline:
    mov al,10
    call serial_send
    jc tx_error
    or bp,bp
    jnz .line
    jmp .end
.last:
    mov al,10
    call serial_send
    jc tx_error
.end:
    mov byte [setup_crc_active],0
    mov si,setup_end
    call setup_send_string
    mov bx,[setup_crc]
    mov cx,4
.hex:
    rol bx,1
    rol bx,1
    rol bx,1
    rol bx,1
    mov al,bl
    and al,15
    add al,'0'
    cmp al,'9'
    jbe .digit
    add al,7
.digit:
    call serial_send
    jc tx_error
    loop .hex
    mov al,10
    call serial_send
    jc tx_error
    jmp receive_begin

setup_send_string:
    lodsb
    or al,al
    jz .done
    call serial_send
    jc tx_error
    jmp setup_send_string
.done:
    ret

setup_filename db 'A:\AISETUP.TXT',0
setup_command db '/reload',0
setup_begin db '@SETUP BEGIN',10,0
setup_prefix db '@SETUP ',0
setup_end db '@SETUP END ',0
startup_setup db 0
setup_success db 'AISETUP.TXT applied.'
setup_crc dw 0ffffh
setup_crc_active db 0
setup_bad_message db 'Cannot read AISETUP.TXT.',13,10,'Use ASCII text, maximum 2048 bytes.',13,10,'Existing settings retained.',13,10,'Press any key.',0
setup_loading_message db 'Applying AISETUP.TXT...',13,10,'Credentials remain hidden.',13,10,0
setup_length dw 0
setup_buffer times 2049 db 0


; Card-backed settings editor: edit a copy in memory; save explicitly.
editor_start:
    mov byte [editor_page],0
    call setup_load
    jc .menu
    mov si,editor_defaults
    mov di,setup_buffer
    xor cx,cx
.default:
    lodsb
    or al,al
    jz .ready
    stosb
    inc cx
    jmp .default
.ready:
    mov [setup_length],cx
.menu:
    call clear
    mov si,editor_menu
    cmp byte [editor_page],0
    je .print_menu
    mov si,editor_options_menu
.print_menu:
    call print
    xor ah,ah
    int 16h
    cmp al,27
    je new_prompt
    cmp al,'s'
    je editor_save
    cmp al,'S'
    je editor_save
    cmp al,'o'
    je .options
    cmp al,'O'
    je .options
    cmp al,'b'
    je .identity_page
    cmp al,'B'
    je .identity_page
    cmp byte [editor_page],0
    je .identity
    cmp al,'v'
    je .voice
    cmp al,'V'
    je .voice
    cmp al,'r'
    je .robot
    cmp al,'R'
    je .robot
    cmp al,'l'
    je .volume
    cmp al,'L'
    je .volume
    cmp al,'t'
    je .station
    cmp al,'T'
    je .station
    cmp al,'g'
    je .gap
    cmp al,'G'
    je .gap
    jmp .menu
.voice:
    mov al,8
    jmp .chosen
.robot:
    mov al,9
    jmp .chosen
.volume:
    mov al,10
    jmp .chosen
.station:
    mov al,11
    jmp .chosen
.gap:
    mov al,12
    jmp .chosen
.options:
    mov byte [editor_page],1
    jmp .menu
.identity_page:
    mov byte [editor_page],0
    jmp .menu
.identity:
    cmp al,'1'
    jb .menu
    cmp al,'8'
    ja .menu
    sub al,'1'
.chosen:
    xor ah,ah
    mov [editor_index],ax
    shl ax,1
    mov bx,ax
    mov si,[editor_fields+bx]
    mov [editor_field],si
    mov ax,[editor_limits+bx]
    mov [editor_limit],ax
    call clear
    call print
    mov si,editor_current
    call print
    call editor_find
    jnc .new
    cmp word [editor_index],5
    je .hidden
    cmp word [editor_index],6
    je .hidden
.current:
    cmp cx,26
    jbe .current_short
    mov cx,26
.current_short:
    jcxz .new
    lodsb
    call char_out
    loop .current
    jmp .new
.hidden:
    mov si,editor_hidden
    call print
.new:
    mov si,editor_new
    call print
    xor cx,cx
    mov di,editor_value
    call editor_redraw
.input:
    xor ah,ah
    int 16h
    cmp al,27
    je .menu
    cmp al,13
    je .entered
    cmp al,8
    je .backspace
    cmp al,32
    jb .input
    cmp al,126
    ja .input
    cmp cx,[editor_limit]
    jae .input
    stosb
    inc cx
    cmp word [editor_index],5
    je .mask
    cmp word [editor_index],6
    jne .echo
.mask:
    mov al,'*'
.echo:
    call editor_redraw
    jmp .input
.backspace:
    jcxz .input
    dec di
    dec cx
    call editor_redraw
    jmp .input
.entered:
    or cx,cx
    jz .menu                ; Blank Enter preserves the old field.
    mov [editor_value_length],cx
    call editor_validate_option
    jc .valid_option
    call clear
    mov si,editor_option_error
    call print
    xor ah,ah
    int 16h
    jmp .menu
.valid_option:
    call editor_replace
    jnc .menu
    mov si,editor_size_error
    call print
    xor ah,ah
    int 16h
    jmp .menu

editor_validate_option:
    push ax
    push bx
    push cx
    push dx
    push si
    mov si,editor_value
    mov ax,[editor_index]
    cmp ax,8
    jb .ok
    cmp ax,9
    ja .number
    cmp cx,2
    jne .off
    cmp word [si],'on'
    je .ok
    jmp .bad
.off:
    cmp cx,3
    jne .bad
    cmp word [si],'of'
    jne .bad
    cmp byte [si+2],'f'
    je .ok
    jmp .bad
.number:
    xor bx,bx
.digit:
    lodsb
    sub al,'0'
    cmp al,9
    ja .bad
    xor ah,ah
    mov dx,bx
    shl bx,1
    shl dx,1
    shl dx,1
    shl dx,1
    add bx,dx
    add bx,ax
    loop .digit
    cmp word [editor_index],12
    je .gap
    cmp word [editor_index],11
    je .station
    cmp bx,100
    jbe .ok
    jmp .bad
.gap:
    cmp bx,4000
    jbe .ok
    jmp .bad
.station:
    cmp bx,1
    jb .bad
    cmp bx,5
    jbe .ok
.bad:
    clc
    jmp .done
.ok:
    stc
.done:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
editor_option_error db 'Invalid option value.',13,10,'Voice/robot: on or off',13,10,'Volume: 0..100   Station: 1..5',13,10,'Preview gap: 0..4000 us',13,10,'Press any key.',0

editor_redraw:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov dx,0300h
    mov ah,2
    xor bh,bh
    int 10h
    mov si,editor_value
    mov dx,cx
    cmp dx,38
    jbe .visible
    add si,dx
    sub si,37
    mov al,'<'
    call char_out
    mov dx,37
    mov bx,37
    jmp .draw
.visible:
    mov bx,38
.draw:
    or dx,dx
    jz .pad
    lodsb
    cmp word [editor_index],5
    je .mask
    cmp word [editor_index],6
    jne .output
.mask:
    mov al,'*'
.output:
    call char_out
    dec dx
    dec bx
    jmp .draw
.pad:
    or bx,bx
    jz .footer
    mov al,' '
    call char_out
    dec bx
    jmp .pad
.footer:
    mov dx,0500h
    mov ah,2
    xor bh,bh
    int 10h
    mov si,editor_edit_help
    call print
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
editor_edit_help db 'Enter: save/keep   Esc: back',0

; SI points at value and CX holds its length on success (CF set).
editor_find:
    mov si,setup_buffer
    mov bp,[setup_length]
.line:
    or bp,bp
    jz .none
    push si
    call editor_match
    pop si
    jnc .skip
    mov di,[editor_field]
.key:
    inc si
    inc di
    cmp byte [di],0
    jne .key
    inc si
    mov di,si
    xor cx,cx
.value:
    cmp si,setup_buffer
    mov ax,si
    sub ax,setup_buffer
    cmp ax,[setup_length]
    jae .found
    cmp byte [si],13
    je .found
    cmp byte [si],10
    je .found
    inc si
    inc cx
    jmp .value
.found:
    mov si,di
    stc
    ret
.skip:
    lodsb
    dec bp
    cmp al,10
    je .line
    or bp,bp
    jnz .skip
.none:
    clc
    ret

; Match exact field= at a line boundary, without reading past the file.
editor_match:
    push si
    push di
    push cx
    mov di,[editor_field]
    mov cx,bp
.loop:
    cmp byte [di],0
    je .equal
    jcxz .no
    lodsb
    cmp al,[di]
    jne .no
    inc di
    dec cx
    jmp .loop
.equal:
    jcxz .no
    cmp byte [si],'='
    jne .no
    stc
    jmp .done
.no:
    clc
.done:
    pop cx
    pop di
    pop si
    ret

editor_replace:
    mov si,setup_buffer
    mov bp,[setup_length]
    mov di,editor_scratch
.line:
    or bp,bp
    jz .append
    call editor_match
    jc .skip
.copy:
    lodsb
    stosb
    dec bp
    cmp al,10
    je .line
    or bp,bp
    jnz .copy
    jmp .append
.skip:
    lodsb
    dec bp
    cmp al,10
    je .line
    or bp,bp
    jnz .skip
.append:
    cmp di,editor_scratch
    je .key
    cmp byte [di-1],10
    je .key
    mov al,10
    stosb
.key:
    mov si,[editor_field]
.keybyte:
    lodsb
    or al,al
    jz .value
    stosb
    jmp .keybyte
.value:
    mov al,'='
    stosb
    mov si,editor_value
    mov cx,[editor_value_length]
    rep movsb
    mov al,13
    stosb
    mov al,10
    stosb
    mov cx,di
    sub cx,editor_scratch
    cmp cx,2048
    ja .too_big
    mov [setup_length],cx
    mov si,editor_scratch
    mov di,setup_buffer
    rep movsb
    clc
    ret
.too_big:
    stc
    ret

editor_save:
    call card_write
    jc editor_save_error
    jmp setup_send

; CF reports failure; caller decides whether to apply settings or only save.
card_write:
    mov dx,editor_temp
    xor cx,cx
    mov ah,3ch
    int 21h
    jc .error
    mov bx,ax
    mov cx,[setup_length]
    mov dx,setup_buffer
    mov ah,40h
    int 21h
    jc .close_error
    cmp ax,[setup_length]
    jne .close_error
    mov ah,3eh
    int 21h
    jc .error
    ; Keep the previous file as AISETUP.BAK before publishing the new file.
    mov dx,editor_backup
    mov ah,41h
    int 21h
    jnc .backup
    cmp ax,2
    jne .error
.backup:
    mov dx,setup_filename
    mov di,editor_backup
    mov ah,56h
    int 21h
    jnc .publish
    cmp ax,2
    jne .error
.publish:
    mov dx,editor_temp
    mov di,setup_filename
    mov ah,56h
    int 21h
    jnc .success
    mov dx,editor_backup
    mov di,setup_filename
    mov ah,56h
    int 21h                 ; Restore old file if publishing fails.
    jmp .error
.close_error:
    mov ah,3eh
    int 21h
.error:
    stc
    ret
.success:
    clc
    ret
editor_save_error:
    call clear
    mov si,editor_write_error
    call print
    xor ah,ah
    int 16h
    jmp new_prompt

editor_command db '/config',0
editor_menu db 'CONFIG: AISETUP.TXT',13,10,'1 Name     2 City      3 Country',13,10,'4 Timezone 5 Wi-Fi',13,10,'6 Password 7 API key   8 Model',13,10,'1-8: edit field. S: save and apply.',13,10,'O: audio/radio options',13,10,'Esc: cancel changes.',0
editor_page db 0
editor_options_menu db 'CONFIG // OPTIONS',13,10,'V Voice on/off   R Robot on/off',13,10,'L Volume 0..100   T Radio station 1..5',13,10,'G Preview gap 0..4000 us',13,10,'S: save all settings',13,10,'B: connection settings   Esc: cancel',0
editor_current db 13,10,'Current: ',0
editor_hidden db '[saved; hidden]',0
editor_new db 13,10,'New value:',13,10,0
editor_size_error db 13,10,'File exceeds 2048 bytes.',0
editor_write_error db 'Cannot save AISETUP.TXT.',13,10,'Check card space/write protection.',13,10,'Settings were not applied.',13,10,'Press any key.',0
editor_temp db 'A:\AISETUP.NEW',0
editor_backup db 'A:\AISETUP.BAK',0
editor_defaults db 'timezone=UTC',13,10,'tz_rule=UTC0',13,10,'voice=on',13,10,'robot=on',13,10,'volume=40',13,10,'radio_station=1',13,10,'frame_gap_us=4000',13,10,0
editor_fields dw editor_name,editor_city,editor_country,editor_timezone,editor_ssid,editor_password,editor_key,editor_model,editor_voice,editor_robot,editor_volume,editor_station,editor_gap
editor_name db 'name',0
editor_city db 'city',0
editor_country db 'country',0
editor_timezone db 'timezone',0
editor_rule db 'tz_rule',0
editor_ssid db 'ssid',0
editor_password db 'wifi_password',0
editor_key db 'api_key',0
editor_model db 'model',0
editor_voice db 'voice',0
editor_robot db 'robot',0
editor_volume db 'volume',0
editor_station db 'radio_station',0
editor_gap db 'frame_gap_us',0
editor_index dw 0
editor_limits dw 80,80,2,80,32,63,512,80,3,3,3,1,4
editor_limit dw 80
editor_field dw 0
editor_value_length dw 0
editor_value times 513 db 0
editor_scratch times 2700 db 0

; Radio is controlled locally; only short commands and status cross RS-232.
radio_start:
    mov di,radio_cache
    xor ax,ax
    mov cx,304
    rep stosb
    mov byte [radio_drawn],0
    mov byte [radio_mode],1
    mov si,radio_fast_start
radio_send:
    call setup_send_string
    mov al,10
    call serial_send
    jc tx_error
    call camera_deadline
    mov di,radio_packet
    mov cx,10
.packet:
    call radio_read_byte
    jc rx_error
    stosb
    loop .packet
    cmp word [radio_packet],5250h
    jne rx_error
    cmp word [radio_packet+2],3144h
    jne rx_error
    mov si,radio_packet+4
    mov cx,4
    mov bx,0ffffh
.crc_byte:
    lodsb
    xor bl,al
    push cx
    mov cx,8
.crc:
    shr bx,1
    jnc .next
    xor bx,0a001h
.next:
    loop .crc
    pop cx
    loop .crc_byte
    cmp bx,[radio_packet+8]
    jne rx_error
    cmp byte [radio_packet+4],4
    ja rx_error
    cmp byte [radio_packet+5],1
    jb rx_error
    cmp byte [radio_packet+5],5
    ja rx_error
    cmp word [radio_packet+6],100
    ja rx_error
    call radio_build_screen
    jmp radio_show
radio_show:
    cmp byte [radio_mode],2
    jne .status
    mov byte [radio_mode],0
    jmp new_prompt
.status:
    cmp byte [radio_drawn],0
    jne .draw
    call clear
    mov byte [radio_drawn],1
.draw:
    ; Fixed 8-row display. Leave two right columns unused: no LCD scrolling.
    mov si,response_buffer
    xor dx,dx
.row:
    ; Compare with the last drawn row. Unchanged labels never redraw.
    push si
    push di
    push cx
    mov al,dh
    mov bl,38
    mul bl
    mov di,radio_cache
    add di,ax
    mov cx,38
    repe cmpsb
    pop cx
    pop di
    pop si
    je .drain
    push si
    push di
    push cx
    mov al,dh
    mov bl,38
    mul bl
    mov di,radio_cache
    add di,ax
    mov cx,38
    rep movsb
    pop cx
    pop di
    pop si
    mov ah,2
    xor bh,bh
    int 10h
    mov cx,38
.characters:
    cmp byte [si],0
    je .pad
    cmp byte [si],10
    je .pad
    lodsb
    call char_out
    loop .characters
    jmp .drain
.pad:
    mov al,' '
    call char_out
    loop .pad
.drain:
    cmp byte [si],0
    je .advance
    lodsb
    cmp al,10
    jne .drain
.advance:
    inc dh
    cmp dh,8
    jb .row
    cmp byte [radio_save_failed],0
    je .saved
    mov dx,0600h
    mov ah,2
    xor bh,bh
    int 10h
    mov si,pref_radio_save_error
    call print
.saved:
    mov byte [radio_ticks],0
radio_wait:
    mov ah,1
    int 16h
    jz .poll
    xor ah,ah
    int 16h
    cmp al,27
    je .background
    or al,al
    jnz radio_wait
    cmp ah,4bh
    je .previous
    cmp ah,4dh
    je .next
    cmp ah,48h
    je .up
    cmp ah,50h
    je .down
    jmp radio_wait
.poll:
    mov ah,2
    int 1ah
    cmp dh,[last_second]
    je radio_wait
    mov [last_second],dh
    inc byte [radio_ticks]
    cmp byte [radio_ticks],3
    jb radio_wait
    call persist_radio_options
    mov si,radio_poll
    jmp radio_send
.previous:
    mov si,radio_previous
    jmp radio_send
.next:
    mov si,radio_next
    jmp radio_send
.up:
    mov si,radio_up
    jmp radio_send
.down:
    mov si,radio_down
    jmp radio_send
.background:
    call persist_radio_options
    mov byte [radio_mode],0
    jmp new_prompt
radio_mode db 0
radio_drawn db 0
radio_ticks db 0
text_frame_started db 0
radio_command db '/radio',0
radio_fast_start db '/rfast',0
radio_previous db '/rbfast',0
radio_next db '/rnfast',0
radio_up db '/rufast',0
radio_down db '/rdfast',0
radio_stop db '/radiostop',0
radio_poll db '/rpfast',0

; Save confirmed shortcut choices to the same text file as /config.
; No save is attempted for an error reply or when the value is unchanged.
persist_command_option:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov si,input_buffer
    mov di,pref_mute
    call pref_equal
    jc .mute
    mov si,input_buffer
    mov di,pref_unmute
    call pref_equal
    jc .unmute
    mov si,input_buffer
    mov di,pref_robot_on
    call pref_equal
    jc .robot_on
    mov si,input_buffer
    mov di,pref_robot_off
    call pref_equal
    jc .robot_off
    mov si,input_buffer
    mov di,pref_vol
    mov cx,5
    repe cmpsb
    jne .done
    mov si,response_buffer
    mov di,pref_vol_ack
    mov cx,16
    repe cmpsb
    jne .done
    mov si,input_buffer+5
    mov bx,10
    jmp .prepare
.mute:
    mov si,response_buffer
    mov di,pref_mute_ack
    mov cx,13
    repe cmpsb
    jne .done
    mov si,pref_off
    mov bx,8
    jmp .prepare
.unmute:
    mov si,response_buffer
    mov di,pref_unmute_ack
    mov cx,25
    repe cmpsb
    jne .done
    mov si,pref_on
    mov bx,8
    jmp .prepare
.robot_on:
    mov si,response_buffer
    mov di,pref_robot_on_ack
    mov cx,27
    repe cmpsb
    jne .done
    mov si,pref_on
    mov bx,9
    jmp .prepare
.robot_off:
    mov si,response_buffer
    mov di,pref_robot_off_ack
    mov cx,17
    repe cmpsb
    jne .done
    mov si,pref_off
    mov bx,9
.prepare:
    mov [editor_index],bx
    shl bx,1
    mov ax,[editor_fields+bx]
    mov [editor_field],ax
    mov di,editor_value
    xor cx,cx
.copy:
    lodsb
    or al,al
    jz .ready
    cmp cx,4
    jae .done
    stosb
    inc cx
    jmp .copy
.ready:
    mov [editor_value_length],cx
    call editor_validate_option
    jnc .done
    call quiet_setup_load
    jnc .failed
    mov byte [pref_changed],0
    call pref_update
    jc .failed
    cmp byte [pref_changed],0
    je .done
    call card_write
    jnc .done
.failed:
    ; Keep UART reply and make a failed card save visible to the Operator.
    mov di,response_buffer
    add di,[response_length]
    cmp word [response_length],4000
    ja .done
    mov si,pref_save_error
.message:
    lodsb
    stosb
    or al,al
    jnz .message
.done:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

pref_equal:
    lodsb
    cmp al,[di]
    jne .no
    inc di
    or al,al
    jnz pref_equal
    stc
    ret
.no:
    clc
    ret

; Update only when different, keeping backup writes off the polling path.
pref_update:
    call editor_find
    jnc .replace
    cmp cx,[editor_value_length]
    jne .replace
    mov di,editor_value
    repe cmpsb
    je .same
.replace:
    call editor_replace
    jc .done
    mov byte [pref_changed],1
.same:
    clc
.done:
    ret

persist_radio_options:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov byte [radio_save_failed],0
    mov si,response_buffer
    mov di,pref_radio_header
    mov cx,15
    repe cmpsb
    jne .done
    cmp byte [response_buffer+33],'1'
    jb .done
    cmp byte [response_buffer+33],'5'
    ja .done
    call quiet_setup_load
    jnc .failed
    mov byte [pref_changed],0
    mov word [editor_field],editor_station
    mov al,[response_buffer+33]
    mov [editor_value],al
    mov word [editor_value_length],1
    call pref_update
    jc .failed
    ; Find the volume meter closing bracket and copy its decimal value.
    mov si,response_buffer
.scan:
    lodsb
    or al,al
    jz .done
    cmp al,']'
    jne .scan
    cmp byte [si],' '
    jne .done
    inc si
    mov di,editor_value
    xor cx,cx
.number:
    lodsb
    cmp al,'/'
    je .volume
    cmp al,'0'
    jb .done
    cmp al,'9'
    ja .done
    cmp cx,3
    jae .done
    stosb
    inc cx
    jmp .number
.volume:
    mov [editor_value_length],cx
    mov word [editor_index],10
    mov word [editor_field],editor_volume
    call editor_validate_option
    jnc .done
    call pref_update
    jc .failed
    cmp byte [pref_changed],0
    je .done
    call card_write
    jnc .done
.failed:
    mov byte [radio_save_failed],1
.done:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
pref_changed db 0
radio_save_failed db 0
pref_mute db '/mute',0
pref_unmute db '/unmute',0
pref_robot_on db '/robot on',0
pref_robot_off db '/robot off',0
pref_vol db '/vol ',0
pref_on db 'on',0
pref_off db 'off',0
pref_vol_ack db 'Speaker volume: '
pref_mute_ack db 'Speech muted.'
pref_unmute_ack db 'Synthetic speech enabled.'
pref_robot_on_ack db 'Robot voice effect enabled.'
pref_robot_off_ack db 'Robot effect off.'
pref_radio_header db 'SKYNET // RADIO'
pref_save_error db 10,'Card save failed. Use /config.',0

pref_radio_save_error db 'CARD SAVE FAILED - use /config         ',0

; AH=scan code. Read BIOS shift state only for up/down arrows.
volume_key:
    push ax
    push bx
    cmp ah,48h
    je .arrow
    cmp ah,50h
    jne .no
.arrow:
    mov bl,ah
    mov ah,2
    int 16h
    test al,3               ; either shift key
    jz .no
    cmp bl,48h
    jne .down
    cmp byte [volume_pending],20
    jge .yes
    inc byte [volume_pending]
    jmp .yes
.down:
    cmp byte [volume_pending],-20
    jle .yes
    dec byte [volume_pending]
.yes:
    pop bx
    pop ax
    stc
    ret
.no:
    pop bx
    pop ax
    clc
    ret

volume_page_key:
    xor ah,ah
    int 16h
    call volume_key
    jnc .done
    call volume_apply
    jmp volume_page_key
.done:
    ret

; A dedicated eight-byte packet is drained before another camera/text request.
; Preserves input text, displayed bitmap, main response and receive deadline.
volume_apply:
    cmp byte [volume_pending],0
    jne .begin
    ret
.begin:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push word [seconds_left]
    xor ax,ax
    mov al,[last_second]
    push ax
.request:
    mov si,volume_up_request
    cmp byte [volume_pending],0
    jg .up
    mov si,volume_down_request
    inc byte [volume_pending]
    jmp .send
.up:
    dec byte [volume_pending]
.send:
    lodsb
    or al,al
    jz .receive
    call serial_send
    jc .failed
    jmp .send
.receive:
    call camera_deadline
    mov di,volume_packet
    mov cx,8
.byte:
    call camera_read_byte
    jc .failed
    stosb
    loop .byte
    cmp word [volume_packet],5650h ; PV
    jne .failed
    cmp word [volume_packet+2],314ch ; L1
    jne .failed
    mov si,volume_packet+4
    mov cx,2
    mov bx,0ffffh
.crc_byte:
    lodsb
    xor bl,al
    push cx
    mov cx,8
.crc:
    shr bx,1
    jnc .next
    xor bx,0a001h
.next:
    loop .crc
    pop cx
    loop .crc_byte
    cmp bx,[volume_packet+6]
    jne .failed
    cmp word [volume_packet+4],100
    ja .failed
    cmp byte [volume_pending],0
    jne .request
    mov byte [volume_card_error],0
    mov byte [volume_link_error],0
    ; Persist only the final confirmed volume, not every repeated key.
    call quiet_setup_load
    jnc .save_failed
    mov word [editor_field],editor_volume
    mov ax,[volume_packet+4]
    mov bx,10
    xor cx,cx
.digits:
    xor dx,dx
    div bx
    push dx
    inc cx
    or ax,ax
    jnz .digits
    mov [editor_value_length],cx
    mov di,editor_value
.decimal:
    pop ax
    add al,'0'
    stosb
    loop .decimal
    mov byte [pref_changed],0
    call pref_update
    jc .save_failed
    cmp byte [pref_changed],0
    je .done
    call card_write
    jnc .done
.save_failed:
    mov byte [volume_card_error],1
    jmp .done
.failed:
    mov byte [volume_pending],0
    mov byte [volume_link_error],1
.done:
    pop ax
    mov [last_second],al
    pop word [seconds_left]
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
volume_pending db 0
volume_card_error db 0
volume_link_error db 0
volume_up_request db '/volumeup',10,0
volume_down_request db '/volumedown',10,0
volume_packet times 8 db 0

quiet_setup db 0
quiet_setup_load:
    mov byte [quiet_setup],1
    call setup_load
    mov byte [quiet_setup],0
    ret

; Small status line on the front page; never overwrite a camera bitmap or reply.
volume_widget:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ah,3
    xor bh,bh
    int 10h
    push dx
    mov dx,0400h
    mov ah,2
    int 10h
    cmp byte [volume_link_error],0
    jne .link_error
    cmp byte [volume_card_error],0
    jne .card_error
    cmp word [volume_packet],5650h
    jne .restore
    mov si,volume_label
    call print
    mov ax,[volume_packet+4]
    mov bx,10
    xor cx,cx
.digits:
    xor dx,dx
    div bx
    push dx
    inc cx
    or ax,ax
    jnz .digits
.print:
    pop ax
    add al,'0'
    call char_out
    loop .print
    mov si,volume_key_help
    call print
    jmp .restore
.link_error:
    mov si,volume_link_message
    call print
    jmp .restore
.card_error:
    mov si,volume_card_message
    call print
.restore:
    pop dx
    mov ah,2
    xor bh,bh
    int 10h
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
volume_label db 'Volume: ',0
volume_key_help db '/100  Shift+Up/Down        ',0
volume_link_message db 'Volume link failed. Restart AI.       ',0
volume_card_message db 'Volume changed; card save failed.     ',0

radio_build_screen:
    mov si,radio_template
    mov di,response_buffer
    mov cx,313
    rep movsb
    mov al,[radio_packet+5]
    add al,'0'
    mov [response_buffer+33],al
    mov byte [response_buffer+37],'5'
    xor ax,ax
    mov al,[radio_packet+4]
    shl ax,1
    mov bx,ax
    mov si,[radio_state_names+bx]
    mov di,response_buffer+78
    call radio_center
    xor ax,ax
    mov al,[radio_packet+5]
    dec ax
    shl ax,1
    mov bx,ax
    mov si,[radio_station_names+bx]
    mov di,response_buffer+117
    call radio_center
    mov ax,[radio_packet+6]
    xor dx,dx
    mov bx,10
    div bx
    mov cx,ax
    mov di,response_buffer+170
    jcxz .digits
.bars:
    mov byte [di],'#'
    inc di
    loop .bars
.digits:
    mov di,response_buffer+182
    mov cx,11
    mov al,' '
    rep stosb
    mov ax,[radio_packet+6]
    mov bx,10
    xor cx,cx
.divide:
    xor dx,dx
    div bx
    push dx
    inc cx
    or ax,ax
    jnz .divide
    mov di,response_buffer+182
.print:
    pop ax
    add al,'0'
    stosb
    loop .print
    mov ax,312fh
    stosw
    mov ax,3030h
    stosw
    ret
radio_center:
    push di
    mov cx,38
    mov al,' '
    rep stosb
    pop di
    push si
    xor cx,cx
.length:
    lodsb
    or al,al
    jz .found
    inc cx
    jmp .length
.found:
    pop si
    mov ax,38
    sub ax,cx
    shr ax,1
    add di,ax
    rep movsb
    ret
radio_packet times 10 db 0
radio_cache times 304 db 0
radio_state_names dw radio_stopped,radio_connecting,radio_playing,radio_unavailable,radio_decoder_error
radio_station_names dw radio_nightride,radio_dark,radio_chill,radio_data,radio_plaza
radio_stopped db 'STOPPED',0
radio_connecting db 'CONNECTING...',0
radio_playing db 'PLAYING',0
radio_unavailable db 'STREAM UNAVAILABLE',0
radio_decoder_error db 'DECODER UNAVAILABLE',0
radio_nightride db 'NIGHTRIDE FM',0
radio_dark db 'DARKSYNTH FM',0
radio_chill db 'CHILLSYNTH FM',0
radio_data db 'DATAWAVE FM',0
radio_plaza db 'NIGHTWAVE PLAZA',0
radio_template db 83,75,89,78,69,84,32,47,47,32,82,65,68,73,79,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,49,32,47,32,51,10,45,45,45,45,45,45,45,45,45,45,45,45,45,45,45,45,45,45,45,45,45,45,45,45,45,45,45,45,45,45,45,45,45,45,45,45,45,45,10,32,32,32,32,32,32,32,32,32,32,32,32,67,79,78,78,69,67,84,73,78,71,46,46,46,32,32,32,32,32,32,32,32,32,32,32,32,32,10,32,32,32,32,32,32,32,32,32,32,32,32,32,78,73,71,72,84,82,73,68,69,32,70,77,32,32,32,32,32,32,32,32,32,32,32,32,32,10,32,32,32,32,32,32,86,79,76,85,77,69,32,91,46,46,46,46,46,46,46,46,46,46,93,32,52,48,47,49,48,48,32,32,32,32,32,32,10,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,10,76,101,102,116,47,82,105,103,104,116,58,32,115,116,97,116,105,111,110,32,32,85,112,47,68,111,119,110,58,32,118,111,108,117,109,101,32,32,10,69,115,99,58,32,98,97,99,107,103,114,111,117,110,100,32,32,32,47,114,97,100,105,111,32,111,102,102,58,32,115,116,111,112,32,32,32,32,10,0

radio_read_byte:
    push bx
    push cx
    push dx
    mov cx,4096
.poll:
    mov dx,[uart_base]
    add dx,5
    in al,dx
    test al,1eh
    jnz .error
    test al,1
    jnz .ready
    ; BIOS keyboard/clock calls can outlast a serial character. Only call
    ; them after a sustained idle interval, not in every inter-byte gap.
    loop .poll
    ; Radio keys remain queued until the packet is validated.
    mov ah,2
    int 1ah
    cmp dh,[last_second]
    je .idle_done
    mov [last_second],dh
    dec word [seconds_left]
    jnz .idle_done
.error:
    stc
    jmp .return
.idle_done:
    mov cx,4096
    jmp .poll
.ready:
    sub dx,5
    in al,dx
    clc
.return:
    pop dx
    pop cx
    pop bx
    ret
