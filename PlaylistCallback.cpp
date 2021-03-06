﻿#include "stdafx.h"

// clang-format off
#include "PlaylistCallback.h"
//#include "EngineThread.fwd.h"
#include "ConfigData.h"
#include "Engine.h"
#include "EngineWindow.h"
// clang-format on

namespace engine {
using coverflow::configData;
using EM = engine::Engine::Messages;

void PlaylistCallback::on_item_focus_change(t_size p_playlist, t_size p_from,
                                            t_size p_to) {
  if (!configData->IsSourcePlaylistOn(p_playlist, PlSrcFilter::ANY_PLAYLIST))
    return;

  if (!configData->CoverFollowsPlaylistSelection)
    return;

  if (p_to != pfc_infinite) {
    src_state srcstate;
    configData->GetState(srcstate);
    configData->GetState(srcstate, false);
    //track is set but focus is not
    //srcstate.track_second.second = playlist_manager::get()->playlist_get_item_handle(p_playlist, p_to);
    srcstate.track_second.first = p_to;
    static_cast<EngineThread*>(this)->send<EM::SourceChangeMessage>(srcstate);
    static_cast<EngineThread*>(this)->send<EM::ReloadCollection>();
  }

}
void PlaylistCallback::on_items_selection_change(t_size p_playlist,
                                                 const bit_array& p_affected,
                                                 const bit_array& p_state) {

  if (!configData->IsSourcePlaylistOn(p_playlist, PlSrcFilter::ANY_PLAYLIST))
    return;

  if (!configData->CoverFollowsPlaylistSelection)
    return;

  metadb_handle_list p_all;
  metadb_handle_list p_selection;

  playlist_manager::get()->playlist_get_all_items(p_playlist, p_all);

  if (p_all.get_count() == 0)
    return; // pasting to an empty playlist?

  t_size ft_affected, ft_state, ft_myselection;

  ft_affected = p_affected.find_first(true, 0, p_all.get_count());
  ft_state = p_state.find_first(true, 0, p_all.get_count());

  if ((ft_state == pfc_infinite) || (ft_affected == pfc_infinite))
    return;  // undefined state

  if (ft_state >= p_all.get_count()) {
    // fp_state = size (first pass of a two-pass deselect/select)
    // or pasting into playlist?
    return;
  }

  if (ft_affected == ft_state) {
    // one-pass initial selection (stock playlis tbrowser)
    // or one-pass selection (first item on list)
    ft_myselection = ft_affected;
  } else
  if (ft_state ==  0) {
    // second pass of a two-pass-selection or one-pass selection
    // JSPlaylist does two-pass-selections, deselect/select
    ft_myselection = ft_affected;
  } else {
    //one-pass selection (stock playlistbrowser)
    ft_myselection = ft_state;
  }

  p_selection.add_item(p_all.get_item(ft_myselection));

  pfc::string8_fast_aggressive titleBuffer, keyBuffer, sortBuffer, db_keyBuffer;
  titleformat_object::ptr titleBuilder, keyBuilder, sortBuilder;
  pfc::stringcvt::string_wide_from_utf8_fast sortBufferWide;

  if (!configData->IsPlaylistSourceModeUngrouped()) {
    //grouped
    titleformat_compiler::get()->compile_safe_ex(keyBuilder, configData->Group);
    p_selection.get_item(0)->format_title(nullptr, keyBuffer, keyBuilder, nullptr);
    titleformat_compiler::get()->compile_safe_ex(sortBuilder, configData->Sort);
    p_selection.get_item(0)->format_title(nullptr, sortBuffer, sortBuilder, nullptr);
    sortBufferWide.convert(sortBuffer);
  } else {
    //ungrouped
    const DBUngroupedParams plparams;
    titleformat_compiler::get()->compile_safe_ex(titleBuilder, configData->SourcePlaylistNGTitle/*plparams.albumtitle*/);
    titleformat_compiler::get()->compile_safe_ex(keyBuilder, plparams.group);
    titleformat_compiler::get()->compile_safe_ex(sortBuilder, plparams.sort);
    p_selection.get_item(0)->format_title(nullptr, titleBuffer, titleBuilder, nullptr);
    p_selection.get_item(0)->format_title(nullptr, keyBuffer, keyBuilder, nullptr);
    p_selection.get_item(0)->format_title(nullptr, sortBuffer, sortBuilder, nullptr);
    sortBufferWide.convert(sortBuffer);

    char tmp_str[4];
    memset(tmp_str, ' ', 3);
    tmp_str[3] = '\0';
    itoa(int(ft_myselection), tmp_str, 10);
    keyBuffer.add_string("|");
    keyBuffer.add_string(tmp_str);
  }

  auto target_albuminfo =
    static_cast<EngineThread*>(this)->sendSync<EM::GetTargetAlbum>().get();

  if (stricmp_utf8(target_albuminfo.value().pos.key.c_str(), keyBuffer) != 0) {
    DBPos sel_pos;
    sel_pos.key = keyBuffer;
    sel_pos.sortKey = sortBufferWide;
    AlbumInfo sel_albuminfo{titleBuffer, sel_pos, p_selection};
    static_cast<EngineThread*>(this)->send<EM::MoveToAlbumMessage>(sel_albuminfo, false);
  }

};

void PlaylistCallback::on_items_added(t_size p_playlist, t_size p_start,
                                      const pfc::list_base_const_t<metadb_handle_ptr>& p_data,
                                      const bit_array& p_selection) {

  if (configData->IsSourcePlaylistOn(p_playlist, PlSrcFilter::ANY_PLAYLIST)) {
    t_size plcount = playlist_manager::get()->playlist_get_item_count(p_playlist);
    if ((plcount == 1 && p_start == 0) || !configData->CoverFollowsPlaylistSelection) {
      static_cast<EngineThread*>(this)->send<EM::ReloadCollection>();
    } else {
      // update on next on_focus_changed()
    }
  }
}

//called quite often if Library Viewer Selection is shown
void PlaylistCallback::on_items_reordered(t_size p_playlist,
                                          const t_size* p_order,
                                          t_size p_count) {

  if (configData->IsSourcePlaylistOn(p_playlist, PlSrcFilter::ANY_PLAYLIST)) {
    static_cast<EngineThread*>(this)->send<EM::ReloadCollection>();
  }
}
void PlaylistCallback::on_items_removed(t_size p_playlist, const bit_array& p_mask,
                                        t_size p_old_count, t_size p_new_count) {

  if (configData->IsSourcePlaylistOn(p_playlist, PlSrcFilter::ANY_PLAYLIST))
    if (p_old_count != p_new_count) {
      if (p_new_count == 0 || !configData->CoverFollowsPlaylistSelection) {
        //not following selection or
        //empty playlist will not call on_focus_changed()
        static_cast<EngineThread*>(this)->send<EM::ReloadCollection>();
      } else {
        //update on next on_focus_changed()
      }
    }
}
// playlist activate and playlist removals
void PlaylistCallback::on_playlist_activate(t_size p_old, t_size p_new) {
  bool brefresh = false;
  src_state srcstate;
  // on playlist deletion callback is invoked twice...
  if (p_new == pfc_infinite) {
    // first callback after playlist removal (p_old is removed, p_new is undefined)
    // if p_old is zero we are deleting the last playlist and wont be a second
    // callback
    if (configData->SourcePlaylist) {
      static_api_ptr_t<playlist_manager> pm;
      pfc::string8 playlistname = configData->SourcePlaylistName;
      t_size playlist = pm->find_playlist(playlistname, playlistname.get_length());
      if (playlist == p_old) {
        configData->GetState(srcstate);
        configData->SourcePlaylistName = coverflow::default_SourcePlaylistName;
        configData->SourcePlaylist = false;
        configData->GetState(srcstate, false);
        brefresh = true;
      }
    }
  } else {
    // second callback after playlist removal, p_new is activated playlist, p_old is
    // undefined or normal playlist activation, p_new <> p_old, both p_new and p_old
    // defined

    if (configData->SourceActivePlaylist) {
      //will only get focus item from callback thread
      configData->GetState(srcstate);
      static_api_ptr_t<playlist_manager> pm;
      pfc::string8 playlistname;
      pm->playlist_get_name(p_new, playlistname);
      configData->SourceActivePlaylistName = playlistname;
      configData->GetState(srcstate, false);
      brefresh = true;
    }
  }
  if (brefresh) {
    if (configData->CoverFollowsPlaylistSelection)
      static_cast<EngineThread*>(this)->send<EM::SourceChangeMessage>(srcstate);
    static_cast<EngineThread*>(this)->send<EM::ReloadCollection>();
  }
}
void PlaylistCallback::on_playlist_renamed(t_size p_index, const char* p_new_name,
                                           t_size p_new_name_len) {
  if (!configData->IsWholeLibrary()) {
    static_api_ptr_t<playlist_manager> pm;
    pfc::string8 playlistname;
    if (configData->SourceActivePlaylist)
      playlistname = configData->SourceActivePlaylistName;
    else
      playlistname = configData->SourcePlaylistName;

    t_size playlist = pm->find_playlist(playlistname, playlistname.get_length());
    if (playlist == ~0) {
      if (configData->SourceActivePlaylist)
        configData->SourceActivePlaylistName = p_new_name;
      else
        configData->SourcePlaylistName = p_new_name;
    }
  }
}
void PlaylistCallback::on_playlists_removed(const bit_array& p_mask, t_size p_old_count,
                                            t_size p_new_count) {
  if (!configData->IsWholeLibrary())
    // manage state with no available playlist
    // other deletions are processed by on_playlist_activate(...)
    if (p_new_count == 0)
      static_cast<EngineThread*>(this)->send<EM::ReloadCollection>();
}

}  // namespace engine
