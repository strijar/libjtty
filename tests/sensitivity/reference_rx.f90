! Test-only bridge to the unmodified production WSJT-X receive pipeline.
subroutine wsjtx_receive(pcm,n,expected,correct,wrong) bind(C)
  use iso_c_binding
  use jtty_mdec, only: npending,pending_first,pending_updates, &
       discard_pending_updates,display_message_text
  implicit none
  integer(c_int), value :: n
  integer(c_int16_t), intent(in) :: pcm(n)
  character(c_char), intent(in) :: expected(81)
  integer(c_int), intent(out) :: correct,wrong
  character(len=80) :: text,got
  integer :: i,npatience,nthreads
  common/patience/npatience,nthreads
  npatience=0; nthreads=1
  text=''
  do i=1,80
    if(expected(i)==c_null_char) exit
    text(i:i)=expected(i)
  enddo
  call discard_pending_updates()
  ! Same reset/scan entry points as the upstream structured-decode test.
  call rjtty_sub(pcm,1,384,950,1050,1000.0,50.0)
  call rjtty_sub(pcm,n,384,950,1050,1000.0,50.0)
  correct=0; wrong=0
  do i=pending_first,pending_first+npending-1
    got=display_message_text(pending_updates(i)%decoded)
    if(trim(got)==trim(text) .and. pending_updates(i)%complete) then
      correct=1
    else
      wrong=wrong+1
    endif
  enddo
  call discard_pending_updates()
end subroutine

subroutine wsjtx_wave(word,f0,wave) bind(C)
  use iso_c_binding
  use tbcc, only: tbcc_encode
  use jtty_tbcc_code_profiles
  implicit none
  integer(c_int64_t), value :: word
  real(c_float), value :: f0
  real(c_float), intent(out) :: wave(22656)
  integer :: payload(34),tones(59),i,nwave
  complex :: dummy(1)
  do i=1,34
    payload(i)=int(ibits(word,34-i,1))
  enddo
  tones(1:13)=[0,2,2,3,0,0,3,2,1,3,1,2,0]
  call tbcc_encode(payload,tones(14:59),JTTY_TBCC_PROFILE_1167_1545_80F)
  nwave=22656
  call gen_jttywave(tones,59,384,2.0,12000.0,f0,dummy,wave,0,nwave)
end subroutine
