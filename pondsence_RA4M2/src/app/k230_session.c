#include "k230_session.h"

#include "../modules/uart_links/uart_links.h"

app_k230_status_t g_app_k230;

static uint32_t s_last_rejected_event_id;

static void app_k230_handle_frame(PondSense_K230_FrameTypeDefData const * frame)
{
    if (PONDSENSE_K230_FRAME_UPLOAD == frame->type)
    {
        if ((frame->event_id != g_app_k230.active_event_id) &&
            (frame->event_id != g_app_k230.last_completed_event_id))
        {
            ++g_app_k230.stale_count;
            return;
        }
        if ((frame->event_id == g_app_k230.last_upload.event_id) &&
            (frame->upload_status == g_app_k230.last_upload.upload_status))
        {
            ++g_app_k230.duplicate_count;
            return;
        }
        g_app_k230.last_upload = *frame;
        ++g_app_k230.upload_count;
        return;
    }

    if (frame->event_id != g_app_k230.active_event_id)
    {
        if ((frame->event_id == g_app_k230.last_completed_event_id) ||
            (frame->event_id == s_last_rejected_event_id))
        {
            ++g_app_k230.duplicate_count;
        }
        else
        {
            ++g_app_k230.stale_count;
        }
        return;
    }

    if (PONDSENSE_K230_FRAME_ACK == frame->type)
    {
        if (APP_K230_WAIT_ACK == g_app_k230.state)
        {
            g_app_k230.state = APP_K230_OBSERVING;
            ++g_app_k230.ack_count;
        }
        else
        {
            ++g_app_k230.duplicate_count;
        }
        return;
    }

    if (PONDSENSE_K230_FRAME_NACK == frame->type)
    {
        if ((APP_K230_WAIT_ACK == g_app_k230.state) ||
            (APP_K230_OBSERVING == g_app_k230.state))
        {
            s_last_rejected_event_id = frame->event_id;
            g_app_k230.active_event_id = 0U;
            g_app_k230.state = APP_K230_REJECTED;
            ++g_app_k230.rejected_count;
        }
        else
        {
            ++g_app_k230.duplicate_count;
        }
        return;
    }

    if (PONDSENSE_K230_FRAME_RESULT == frame->type)
    {
        if ((APP_K230_WAIT_ACK == g_app_k230.state) ||
            (APP_K230_OBSERVING == g_app_k230.state))
        {
            g_app_k230.last_result = *frame;
            g_app_k230.last_completed_event_id = frame->event_id;
            g_app_k230.active_event_id = 0U;
            g_app_k230.state = APP_K230_COMPLETE;
            ++g_app_k230.completed_count;
        }
        else
        {
            ++g_app_k230.duplicate_count;
        }
        return;
    }

    ++g_app_k230.unexpected_count;
}

void App_K230_Init(void)
{
    g_app_k230.state = APP_K230_IDLE;
    g_app_k230.next_event_id = 1U;
    Uart_Links_RegisterK230Handler(app_k230_handle_frame);
}

fsp_err_t App_K230_StartObserve(uint32_t now_ms)
{
    ++g_app_k230.request_count;
    if ((APP_K230_WAIT_ACK == g_app_k230.state) ||
        (APP_K230_OBSERVING == g_app_k230.state))
    {
        ++g_app_k230.busy_request_count;
        return FSP_ERR_IN_USE;
    }

    uint32_t const event_id = g_app_k230.next_event_id;
    fsp_err_t const result = Uart_Links_K230SendObserve(event_id);
    g_app_k230.last_write_result = (uint32_t) result;
    if (FSP_SUCCESS != result)
    {
        g_app_k230.state = APP_K230_SEND_ERROR;
        ++g_app_k230.send_error_count;
        return result;
    }

    g_app_k230.active_event_id = event_id;
    g_app_k230.started_ms = now_ms;
    g_app_k230.state = APP_K230_WAIT_ACK;
    g_app_k230.next_event_id = (UINT32_MAX == event_id) ? 1U : event_id + 1U;
    return FSP_SUCCESS;
}

void App_K230_Process(uint32_t now_ms)
{
    if (((APP_K230_WAIT_ACK == g_app_k230.state) ||
         (APP_K230_OBSERVING == g_app_k230.state)) &&
        ((uint32_t) (now_ms - g_app_k230.started_ms) >= APP_K230_RESULT_TIMEOUT_MS))
    {
        g_app_k230.active_event_id = 0U;
        g_app_k230.state = APP_K230_TIMED_OUT;
        ++g_app_k230.timeout_count;
    }

}
