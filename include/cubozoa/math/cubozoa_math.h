#ifndef CBZ_MATH_H_
#define CBZ_MATH_H_

#include <glm/glm.hpp>

namespace cbz::math {

struct Ray {
  glm::vec3 origin;
  glm::vec3 dir;
};

} // namespace cbz::math

#endif
