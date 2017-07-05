// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2017 Intel Corporation. All Rights Reserved.

#include <librealsense/rs2.hpp>
#include <core/debug.h>
#include "record_device.h"

rsimpl2::record_device::record_device(std::shared_ptr<rsimpl2::device_interface> device,
                                      std::shared_ptr<rsimpl2::device_serializer::writer> serializer):
    m_write_thread([](){return std::make_shared<dispatcher>(std::numeric_limits<unsigned int>::max());}),
    m_is_first_event(true),
    m_is_recording(true),
    m_record_pause_time(0),
    m_device(device),
    m_writer(serializer)
{
    if (device == nullptr)
    {
        throw std::invalid_argument("device");
    }

    if (serializer == nullptr)
    {
        throw std::invalid_argument("serializer");
    }

    serializer->reset();

    for (size_t i = 0; i < m_device->get_sensors_count(); i++)
    {
        auto& sensor = m_device->get_sensor(i);

        auto recording_sensor = std::make_shared<record_sensor>(sensor,
                                                                [this, i](std::shared_ptr<frame_interface> f)
                                                                {
                                                                    std::call_once(m_first_call_flag, [this]()
                                                                    {
                                                                        m_capture_time_base = std::chrono::high_resolution_clock::now();
                                                                        m_cached_data_size = 0;
                                                                    });

                                                                    write_data(i, f);
                                                                });
        m_sensors.push_back(recording_sensor);
    }
}

rsimpl2::record_device::~record_device()
{
    (*m_write_thread)->stop();
}
rsimpl2::sensor_interface& rsimpl2::record_device::get_sensor(size_t i)
{
    return *(m_sensors[i]);
}
size_t rsimpl2::record_device::get_sensors_count() const
{
    return m_sensors.size();
}

//template <typename T, typename P>
//bool Is(P* device)
//{
//    return dynamic_cast<T*>(device) != nullptr;
//}

void rsimpl2::record_device::write_header()
{
    auto device_extensions_md = get_extensions_snapshots(m_device.get());

    std::vector<sensor_metadata> sensors_md;
    for (size_t j = 0; j < m_device->get_sensors_count(); ++j)
    {
        auto& sensor = m_device->get_sensor(j);
        auto sensor_extensions_md = get_extensions_snapshots(&sensor);
        sensors_md.emplace_back(sensor_extensions_md);
    }

    m_writer->write_device_description({device_extensions_md, sensors_md});
}

std::chrono::nanoseconds rsimpl2::record_device::get_capture_time()
{
    auto now = std::chrono::high_resolution_clock::now();
    return (now - m_capture_time_base) - m_record_pause_time;
}

void rsimpl2::record_device::write_data(size_t sensor_index, std::shared_ptr<frame_interface> f)
{
    uint64_t data_size = f->get_data_size();
    uint64_t cached_data_size = m_cached_data_size + data_size;
    if (cached_data_size > MAX_CACHED_DATA_SIZE)
    {
        LOG_ERROR("frame drop occurred");
        return;
    }

    m_cached_data_size = cached_data_size;
    auto capture_time = get_capture_time();

    (*m_write_thread)->invoke([this, sensor_index, capture_time, f, data_size](dispatcher::cancellable_timer t)
                                 {
                                     if(m_is_recording == false)
                                     {
                                         return;
                                     }

                                     if(m_is_first_event)
                                     {
                                         try
                                         {
                                             write_header();
                                             m_writer->write({capture_time, sensor_index, f});
                                         }
                                         catch (const std::exception& e)
                                         {
                                             LOG_ERROR("Error read thread");
                                         }

                                         m_is_first_event = false;
                                     }

                                     std::lock_guard<std::mutex> locker(m_mutex);
                                     m_cached_data_size -= data_size;
                                 });
}
const std::string& rsimpl2::record_device::get_info(rs2_camera_info info) const
{
    throw not_implemented_exception(__FUNCTION__);
}
bool rsimpl2::record_device::supports_info(rs2_camera_info info) const
{
    throw not_implemented_exception(__FUNCTION__);
}
const rsimpl2::sensor_interface& rsimpl2::record_device::get_sensor(size_t i) const
{
    throw not_implemented_exception(__FUNCTION__);
}
void rsimpl2::record_device::hardware_reset()
{
    throw not_implemented_exception(__FUNCTION__);
}
rs2_extrinsics rsimpl2::record_device::get_extrinsics(size_t from,
                                                      rs2_stream from_stream,
                                                      size_t to,
                                                      rs2_stream to_stream) const
{
    throw not_implemented_exception(__FUNCTION__);
}

template<typename T>
std::vector<std::shared_ptr<rsimpl2::extension_snapshot>> rsimpl2::record_device::get_extensions_snapshots(T* extendable)
{
    std::vector<std::shared_ptr<extension_snapshot>> sensor_extensions_md;
    for (int i =0 ; i < static_cast<int>(RS2_EXTENSION_TYPE_COUNT); ++i)
    {
        rs2_extension_type ext = static_cast<rs2_extension_type>(i);
        if (ext == RS2_EXTENSION_TYPE_UNKNOWN) continue;
        if (ext == RS2_EXTENSION_TYPE_INFO)
        {
            auto info_api = dynamic_cast<rsimpl2::info_interface*>(extendable);
            if (info_api)
            {
                auto inf = std::make_shared<info_snapshot>(info_api);
                sensor_extensions_md.push_back(inf);
            }
            continue;
        }
        if (ext == RS2_EXTENSION_TYPE_OPTIONS)
        {
            continue;
        }
        //TODO: add other cases
        //        switch (ext)
//        {
//            case RS2_EXTENSION_TYPE_DEBUG:     { rsimpl2::debug_interface                 }
//            case RS2_EXTENSION_TYPE_INFO:
//            break;
//            case RS2_EXTENSION_TYPE_MOTION:    { rsimpl2::motion_sensor_interface         }
//            case RS2_EXTENSION_TYPE_OPTIONS:   { rsimpl2::options_interface               }
//            case RS2_EXTENSION_TYPE_VIDEO:     { rsimpl2::video_sensor_interface          }
//            case RS2_EXTENSION_TYPE_ROI:       { rsimpl2::roi_sensor_interface            }
//            default:
//                throw invalid_value_exception(std::string("Unhandled extension typs:") + std::to_string(i));
//        }
    }
    return sensor_extensions_md;
}
void* rsimpl2::record_device::extend_to(rs2_extension_type extension_type)
{
    return nullptr;
}

rsimpl2::record_sensor::~record_sensor()
{

}
rsimpl2::record_sensor::record_sensor(sensor_interface& sensor,
                                      rsimpl2::record_sensor::frame_interface_callback_t on_frame):
    m_sensor(sensor),
    m_record_callback(on_frame),
    m_is_pause(false),
    m_is_recording(false)
{

}
std::vector<rsimpl2::stream_profile> rsimpl2::record_sensor::get_principal_requests()
{
    m_sensor.get_principal_requests();
}
void rsimpl2::record_sensor::open(const std::vector<rsimpl2::stream_profile>& requests)
{
    m_sensor.open(requests);
}
void rsimpl2::record_sensor::close()
{
    m_sensor.close();
}
rsimpl2::option& rsimpl2::record_sensor::get_option(rs2_option id)
{
    m_sensor.get_option(id);
}
const rsimpl2::option& rsimpl2::record_sensor::get_option(rs2_option id) const
{
    m_sensor.get_option(id);
}
const std::string& rsimpl2::record_sensor::get_info(rs2_camera_info info) const
{
    m_sensor.get_info(info);
}
bool rsimpl2::record_sensor::supports_info(rs2_camera_info info) const
{
    m_sensor.supports_info(info);
}
bool rsimpl2::record_sensor::supports_option(rs2_option id) const
{
    m_sensor.supports_option(id);
}

void rsimpl2::record_sensor::register_notifications_callback(rsimpl2::notifications_callback_ptr callback)
{
    m_sensor.register_notifications_callback(std::move(callback));
}

class my_frame_callback : public rs2_frame_callback
{
    std::function<void(rs2_frame*)> on_frame_function;
public:
    explicit my_frame_callback(std::function<void(rs2_frame*)> on_frame) : on_frame_function(on_frame) {}

    void on_frame(rs2_frame * fref) override
    {
        on_frame_function(fref);
    }

    void release() override { delete this; }
};
void rsimpl2::record_sensor::start(frame_callback_ptr callback)
{
    if(m_frame_callback != nullptr)
    {
        return; //already started
    }

    frame_callback f;
    auto record_cb = [this, callback](rs2_frame* f)
    {
        m_record_callback(std::make_shared<mock_frame>(m_sensor, f->get()));
        callback->on_frame(f);
    };
    m_frame_callback = std::make_shared<my_frame_callback>(record_cb);

    m_sensor.start(m_frame_callback);
}
void rsimpl2::record_sensor::stop()
{
    m_sensor.stop();
    m_frame_callback.reset();
}
bool rsimpl2::record_sensor::is_streaming() const
{
    return m_sensor.is_streaming();
}
void* rsimpl2::record_sensor::extend_to(rs2_extension_type extension_type)
{
    switch (extension_type)
    {

        case RS2_EXTENSION_TYPE_DEBUG:
        {
//            if(m_extensions.find())
//            {
//                return m_extensions[extension_type].get();
//            }
            auto ptr = dynamic_cast<debug_interface*>(&m_sensor);
            if(!ptr)
            {
                throw invalid_value_exception(std::string("Sensor is not of type ") + typeid(debug_interface).name());
            }
            std::shared_ptr<debug_interface> api;
            ptr->recordable<debug_interface>::create_recordable(api, [this](std::shared_ptr<extension_snapshot> e)
            {
                m_record_callback(std::make_shared<extension_snapshot_frame>(m_sensor, e));
            });
            //m_extensions[extension_type] = d;
            //TODO: Verify this doesn't result in memory leaks
            return api.get();
        }
        case RS2_EXTENSION_TYPE_INFO:break;
        case RS2_EXTENSION_TYPE_MOTION:break;
        case RS2_EXTENSION_TYPE_OPTIONS:break;
        //case RS2_EXTENSION_TYPE_UNKNOWN:break;
        case RS2_EXTENSION_TYPE_VIDEO:break;
        case RS2_EXTENSION_TYPE_ROI:break;
        //case RS2_EXTENSION_TYPE_COUNT:break;
        default:
            throw invalid_value_exception(std::string("extension_type ") + std::to_string(extension_type) + " is not supported");
    }
}

double rsimpl2::mock_frame::get_timestamp() const
{
    return 0;
}
rs2_timestamp_domain rsimpl2::mock_frame::get_timestamp_domain() const
{
    return rs2_timestamp_domain::RS2_TIMESTAMP_DOMAIN_SYSTEM_TIME;
}
unsigned int rsimpl2::mock_frame::get_stream_index() const
{
    return 0;
}
const uint8_t* rsimpl2::mock_frame::get_data() const
{
    return m_frame->data.data();
}
size_t rsimpl2::mock_frame::get_data_size() const
{
    return m_frame->data.size();
}
const rsimpl2::sensor_interface& rsimpl2::mock_frame::get_sensor() const
{
    return m_sensor;
}
rsimpl2::mock_frame::mock_frame(rsimpl2::sensor_interface& s, frame* f) :
    m_sensor(s),
    m_frame(f)
{

}
rsimpl2::ros_device_serializer_impl::ros_device_serializer_impl(std::string file):
    m_file(file)
{
    //TODO have stream_writer throw this error
    if(!std::ofstream(file).good())
    {
        throw std::invalid_argument(std::string("File ") + file + " is invalid or cannot be opened");
    }
}

std::shared_ptr<rsimpl2::device_serializer::writer> rsimpl2::ros_device_serializer_impl::get_writer()
{
    throw not_implemented_exception(__FUNCTION__);
}
std::shared_ptr<rsimpl2::device_serializer::writer> rsimpl2::ros_device_serializer_impl::get_reader()
{
    throw not_implemented_exception(__FUNCTION__);
}

void rsimpl2::ros_device_serializer_impl::ros_writer::write_device_description(const rsimpl2::device_snapshot& device_description)
{
    throw not_implemented_exception(__FUNCTION__);
}
void rsimpl2::ros_device_serializer_impl::ros_writer::write(rsimpl2::device_serializer::storage_data data)
{
    throw not_implemented_exception(__FUNCTION__);
}
void rsimpl2::ros_device_serializer_impl::ros_writer::reset()
{
    throw not_implemented_exception(__FUNCTION__);
}
rsimpl2::device_snapshot rsimpl2::ros_device_serializer_impl::ros_reader::query_device_description()
{
    throw not_implemented_exception(__FUNCTION__);
}
rsimpl2::device_serializer::storage_data rsimpl2::ros_device_serializer_impl::ros_reader::read()
{
    throw not_implemented_exception(__FUNCTION__);
}
void rsimpl2::ros_device_serializer_impl::ros_reader::seek_to_time(std::chrono::nanoseconds time)
{
    throw not_implemented_exception(__FUNCTION__);
}
std::chrono::nanoseconds rsimpl2::ros_device_serializer_impl::ros_reader::query_duration() const
{
    throw not_implemented_exception(__FUNCTION__);
}
void rsimpl2::ros_device_serializer_impl::ros_reader::reset()
{

}