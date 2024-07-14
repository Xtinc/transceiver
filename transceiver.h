#ifndef AUDIO_TRANSCERIVER_HEADER
#define AUDIO_TRANSCERIVER_HEADER

#include "asio.hpp"
#include <atomic>
#include <condition_variable>
#include <map>
#include <mutex>

void start_sound_control();
void close_sound_control();

class Session
{
    struct lock
    {
        explicit lock(std::atomic_flag &flag) : m_flag(flag)
        {
            while (m_flag.test_and_set(std::memory_order_acquire))
                ;
        }

        ~lock()
        {
            m_flag.clear(std::memory_order_release);
        }

      private:
        std::atomic_flag &m_flag;
    };

  public:
    Session(size_t blk_sz, size_t blk_num, int _chan) : chan(_chan), ready{ATOMIC_FLAG_INIT}, os(&buf), is(&buf)
    {
        out_buf = new char[blk_sz];
        buf.prepare(blk_sz * blk_num);
    }

    ~Session()
    {
        delete[] out_buf;
    }

    void assemble_pack(char id, const char *data, size_t len)
    {
        os << id << (char)chan << (char)0xab << (char)0xcd << (char)0x0 << (char)0x0 << (char)0x0 << (char)0x0;
        os.write(data, len);
    }

    void store_data(const char *data, size_t len)
    {
        lock spin(ready);
        os.write(data, len);
    }

    void load_data(size_t len)
    {
        std::memset(out_buf, 0, len);
        lock spin(ready);
        if (buf.size() >= len)
        {
            is.read(out_buf, len);
        }
    }

  public:
    char *out_buf;
    asio::streambuf buf;
    const int chan;

  private:
    std::atomic_flag ready;
    std::ostream os;
    std::istream is;
};

class TransCeiver
{
    using asio_udp = asio::ip::udp;
    using session_ptr = std::unique_ptr<Session>;

  public:
    TransCeiver(char _token, short _port, int _sample_rate, int _period);
    ~TransCeiver();

  public:
    bool connect(const std::string &ip, short port);

    void start();

    void stop();

    void play(const std::string &wav_file);

    void send_pcm_frames(const void *input);

    void recv_pcm_frames(void *output);

  private:
    void do_receive();

    bool validate_pack() const;

  private:
    const char token;
    void *ios;
    const int period;
    int ichan_num;
    int ochan_num;
    int sample_rate;

    std::array<char, 9680> recv_buf;
    asio::io_context io_context;
    asio::ip::udp::socket sock;
    asio::ip::udp::endpoint dest;
    session_ptr send_session;
    std::map<char, session_ptr> sessions;
    std::unique_ptr<std::thread> io_thd;
};

#endif