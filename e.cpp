bool setup( driver_info_t& netio, driver_info_t& vic ) {
  dbg( "Filling PTE hierarchy\n" );
  fill_pte_hierarchy( );

  auto const remove_page = [ & ]( u64 pfn ) -> bool {
    auto const irql = __readcr8( );

    __writecr8( DISPATCH_LEVEL );
    auto const status = fn::mi_remove_physical_memory_fn( pfn, 1, 0x10 );

    if ( !NT_SUCCESS( status ) ) {
      dbg( "Failed to remove physical memory for pfn %#016llx, status: %#08x\n", pfn, status );
      goto ret;
    } else {
      dbg( "PFN %#016llx removed from physical memory.\n", pfn );
    }

    __writecr8( irql );

    auto const mmpfn = mmpfn_for_phys( pfn << 12ull );
    auto const prev = fn::mi_lock_page_inline_fn( ( u64 )mmpfn );
    auto const opte = ( u64* )( ( u64 )mmpfn + 0x10 );
    auto const ref = ( i16 volatile* )( ( u64 )mmpfn + 0x20 );

    dbg( "Previous reference count for pfn %#016llx : %u\n", mmpfn, *ref );

    *ref += 1224u;

    dbg( "New reference count for pfn %#016llx : %u\n", mmpfn, *ref );

    fn::mi_unlock_page_fn( ( u64 )mmpfn, prev );

    ret:
    return status == STATUS_SUCCESS;
  };

  auto const pte_type = ( u64 )fn::mi_state + SYSPTE_INFO_COMPLETE_OFFSET_1288;

  dbg( "MiState: %#016llx PTE Type: %#016llx\n", ( u64 )fn::mi_state, pte_type );

  auto const va = fn::mi_obtain_session_va_fn( pg_count, _MI_SYSTEM_VA_TYPE::MiVaPagedPool );

  dbg( "va=%#016llx:px=%#04x:pp=%#04x:pd=%#04x\n", va, ( va >> 39u ) & 0x1ff, ( va >> 30u ) & 0x1ff, ( va >> 21u ) & 0x1ff );

  auto const pd = pde_for_va( va );
  auto       pde = fn::mi_get_page_fn( fn::mi_system_partition, 0, 8 );

  dbg( "pd=%#016llx\n", ( u64 )pd );

  auto const a2 = 0x00000000ffffffff;

  fn::mi_initialize_pfn_fn( ( void* )mmpfn_for_phys( pde << page_shift ), ( void* )( &a2 ), 0, 4 );

  buf = ( u8* )va;
  ptes = pte_for_va( va );

  ENSURE_PAGE( pde );

  {
    u64 const pages[] = { pde };

    for ( auto& p : pages ) {
      auto const v = fn::mm_map_io_space_fn( { .QuadPart = ( long long )( p << page_shift ) }, page_size, MmNonCached );

      if ( !v ) continue;

      auto const pfn = mmpfn_for_phys( pde << page_shift );

      NT_ASSERT( v != nullptr );

      dbg( "[%u] Zeroing out %#016llx (%#08llx) | %#016llx\n",
           __readcr8( ),
           ( ULONG_PTR )v,
           p,
           ( uintptr_t )( fn::mi_lock_page_inline_fn ) );

      memset( v, 0, page_size );

      fn::mm_unmap_io_space_fn( v, page_size );

      if ( !remove_page( p ) ) {
        if constexpr ( is_debug ) __debugbreak( );
        return false;
      }

      // dbg( "Mapped page %#016llx (%#016llx) | %u\n", pfn, v, __readcr8( ) );

      // auto const old_irql = __readcr8( );
      //__writecr8( DISPATCH_LEVEL );
      // auto const irql = fn::mi_lock_page_inline_fn( ( u64 )pfn );
      //__writecr8( old_irql );

      // dbg( "Completed %#016llx\n", pfn );
    }
  }

  dbg( "%#016llx (%#016llx) | %u\n", pd, pde, __readcr8( ) );

  pd->addr = 0;
  pd->present = 1;
  pd->write = 1;
  pd->pfn = pde;

  _mm_sfence( );

  __invlpg( buf );

  for ( auto n = 0u; n < pg_count; n++ ) {
    auto const e = &ptes[ n ];
    auto const mirror = ( void* )pte_for_va( vic.base + n * page_size );
    auto       page = fn::mi_get_page_fn( fn::mi_system_partition, 0, 8 );

    ENSURE_PAGE( page );

    if ( !remove_page( page ) ) {
      if constexpr ( is_debug ) __debugbreak( );
      return false;
    }

    auto pfn = ( void* )mmpfn_for_phys( page << page_shift );

    fn::mi_initialize_pfn_fn( pfn, mirror, 0, 4 );

    e->addr = 0;
    e->write = true;
    e->xd = true;
    e->pfn = page;
    e->addr |= ( 0xallu << 56 );
    e->global = true;
    e->present = true;
  }

  fn::ke_initialize_mutex( &lock, 0 );
  fn::ke_initialize_timer( &cycle );

  auto const proc = KeGetCurrentProcessorNumber( );
  auto const vproc = randint_range( proc + 1ul, fn::ke_number_of_processors );

  fn::ke_initialize_dpc( &dpc, detail::run, nullptr );

  dpc.Type = 19;
  dpc.Importance = LowImportance;

  fn::ke_set_target_processor_dpc( dpc, vproc );
  fn::ke_set_timer_ex( &cycle, { .QuadPart = MS_TO_100NS( 1000 ) }, 1, &dpc );

  return true;
}
