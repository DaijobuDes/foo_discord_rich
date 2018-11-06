#include <stdafx.h>
#include "discord_impl.h"

#include <ui/ui_pref.h>

#include <discord_rpc.h>

#include <ctime>

using pref = drp::ui::CDialogPref;


namespace
{

class DiscordDataHandler
{
public:
    DiscordDataHandler()
    {
        memset( &presence_, 0, sizeof( presence_ ) );
    }

    void Initialize()
    {
        DiscordEventHandlers handlers;
        memset( &handlers, 0, sizeof( handlers ) );

        Discord_Initialize( "507982587416018945", &handlers, 1, nullptr );
    }

    void Finalize()
    {
        Discord_Shutdown();
    }

public:
    void UpdateTrackData( metadb_handle_ptr metadb )
    {
        state_.reset();
        details_.reset();
        partyId_.reset();
        trackLength_ = 0;
        needToRefreshTime_ = true;

        auto pc = playback_control::get();        
        auto queryData = [&pc, &metadb]( const char* query )
        {
            titleformat_object::ptr tf;
            titleformat_compiler::get()->compile_safe( tf, query );
            pfc::string8_fast result;

            if ( pc->is_playing() )
            {
                metadb_handle_ptr dummyHandle;
                pc->playback_format_title_ex( dummyHandle, nullptr, result, tf, nullptr, playback_control::display_level_all );
            }
            else if ( metadb.is_valid() )
            {
                metadb->format_title( nullptr, result, tf, nullptr );
            }

            return result;
        };

        state_ = queryData( pref::GetStateQuery().c_str() );
        details_ = queryData( pref::GetDetailsQuery().c_str() );
        partyId_ = queryData( pref::GetPartyIdQuery().c_str() );
        pfc::string8_fast lengthStr = queryData( "[%length_seconds%]" );
        pfc::string8_fast durationStr = queryData( "[%playback_time_seconds%]" );

        trackLength_ = ( lengthStr.is_empty() ? 0 : stoll( std::string( lengthStr ) ) );

        presence_.state = state_;
        presence_.details = details_;
        presence_.partyId = partyId_;
        SetDurationInPresence( durationStr.is_empty() ? 0 : stold( std::string( durationStr ) ) );

        UpdatePresense();
    }

    void DisableTrackData()
    {
        Discord_ClearPresence();
    }

    void UpdateDuration( double time, bool force = false )
    {
        if ( !needToRefreshTime_ && !force )
        {
            return;
        }

        SetDurationInPresence( time );
        UpdatePresense();
    }

    void DisableDuration()
    {
        presence_.startTimestamp = 0;
        presence_.endTimestamp = 0;
        UpdatePresense();
    }

    void RequestTimeRefresh()
    {
        needToRefreshTime_ = true;
    }

private:
    void SetDurationInPresence( double time )
    {
        pref::TimeSetting timeSetting = ( trackLength_ ? pref::GetTimeSetting() : pref::TimeSetting::Disabled );

        switch ( timeSetting )
        {
        case pref::TimeSetting::Elapsed:
        {
            presence_.startTimestamp = std::time( nullptr ) - std::llround( time );
            presence_.endTimestamp = 0;

            break;
        }
        case pref::TimeSetting::Remaining:
        {
            presence_.startTimestamp = 0;
            presence_.endTimestamp = std::time( nullptr ) + std::max<uint64_t>( 0, ( trackLength_ - std::llround( time ) ) );

            break;
        }
        case pref::TimeSetting::Disabled:
        {
            presence_.startTimestamp = 0;
            presence_.endTimestamp = 0;

            break;
        }
        }
    }

    void UpdatePresense()
    {
        // TODO: if we can detect preferences changes, then move it to that callback instead
        switch ( pref::GetImageSettings() )
        {
        case pref::ImageSetting::Light:
        case pref::ImageSetting::Dark:
        {
            presence_.largeImageKey = "foobar2000";
            break;
        }
        case pref::ImageSetting::Disabled:
        {
            presence_.largeImageKey = nullptr;
            break;
        }
        }

        if ( pref::IsEnabled() )
        {
            Discord_UpdatePresence( &presence_ );
        }
        else
        {
            Discord_ClearPresence();
        }
    }

private:
    DiscordRichPresence presence_;

    bool needToRefreshTime_ = false;
    pfc::string8_fast state_;
    pfc::string8_fast details_;
    pfc::string8_fast partyId_;
    uint64_t trackLength_ = 0;
};

DiscordDataHandler g_discordHandler;

} // namespace

namespace
{

class PlaybackCallback : public play_callback_static
{
public:
    unsigned get_flags() override
    {
        return flag_on_playback_all;
    }
    void on_playback_dynamic_info( const file_info& info ) override
    {
    }
    void on_playback_dynamic_info_track( const file_info& info ) override
    {
        g_discordHandler.UpdateTrackData( metadb_handle_ptr() );
    }
    void on_playback_edited( metadb_handle_ptr track ) override
    {
        g_discordHandler.UpdateTrackData( track );
    }
    void on_playback_new_track( metadb_handle_ptr track ) override
    {
        g_discordHandler.UpdateTrackData( track );
    }
    void on_playback_pause( bool state ) override
    {
        if ( state )
        {
            g_discordHandler.DisableDuration();
        }
        else
        {
            g_discordHandler.RequestTimeRefresh();
        }
    }
    void on_playback_seek( double time ) override
    {
        g_discordHandler.UpdateDuration( time, true );
        g_discordHandler.RequestTimeRefresh(); ///< track seeking might take some time, thus on_playback_time is needed
    }
    void on_playback_starting( play_control::t_track_command cmd, bool paused ) override
    {
    }
    void on_playback_stop( play_control::t_stop_reason reason ) override
    {
        if ( play_control::t_stop_reason::stop_reason_starting_another != reason )
        {
            g_discordHandler.DisableTrackData();
        }
    }
    void on_playback_time( double time ) override
    {
        g_discordHandler.UpdateDuration( time );
    }
    void on_volume_change( float p_new_val ) override
    {
    }
};

play_callback_static_factory_t<PlaybackCallback> playbackCallback;

} // namespace

namespace discord
{

void InitializeDiscord()
{
    g_discordHandler.Initialize();
}

void FinalizeDiscord()
{
    g_discordHandler.Finalize();
}

} // namespace discord
