#ifndef __CPU_O3_DECOUPLEDIO_HH__
#define __CPU_O3_DECOUPLEDIO_HH__

class DecoupledIO
{
    private:
        bool _valid;
        bool _ready;
        bool _fire;

        bool _last_valid;
        bool _last_ready;
        bool _last_fire;
    public:
        DecoupledIO(bool v, bool r)
            : _valid(v), _ready(r), _fire(false),
              _last_valid(false),
              _last_ready(true),
              _last_fire(false)
        {}

        DecoupledIO()
            : _valid(false), _ready(true), _fire(false),
              _last_valid(false),
              _last_ready(true),
              _last_fire(false)
        {}

        bool valid() { return _valid; }
        void valid(bool v) { _valid = v; }

        bool ready() { return _ready; }
        void ready(bool r) { _ready = r; }

        bool fire() { return _fire; }
        void fire(bool f) { _fire = f; }

        bool lastValid() { return _last_valid; }
        void lastValid(bool v) { _last_valid = v; }

        bool lastReady() { return _last_ready; }
        void lastReady(bool r) { _last_ready = r; }

        bool lastFire() { return _last_fire; }
        void lastFire(bool f) { _last_fire = f; }

        void reset() {
            _valid = false;
            _ready = true;
            _fire = false;
            _last_valid = false;
            _last_ready = true;
            _last_fire = false;
        }
};

#endif //__CPU_O3_DECOUPLEDIO_HH__
