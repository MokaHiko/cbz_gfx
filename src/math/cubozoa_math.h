#ifndef CBZ_MATH_H_
#define CBZ_MATH_H_

#include <glm/glm.hpp>

namespace cbz {
namespace math {

struct Ray {
  glm::vec3 origin;
  glm::vec3 dir;
};

}; // namespace math
} // namespace cbz

#endif
