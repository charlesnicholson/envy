#pragma once

namespace envy {

class engine;
struct pkg;

void run_spec_fetch_phase(pkg *p, engine &eng);

}  // namespace envy
