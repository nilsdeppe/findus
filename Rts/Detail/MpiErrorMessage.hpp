// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <string>

namespace rts::detail {
/*!
 * \brief Returns a human-readable error message for a given MPI error code.
 *
 * This function wraps the MPI function `MPI_Error_string` to convert an MPI
 * error code (such as those returned by most MPI routines) into a descriptive
 * string. This is useful for logging or debugging MPI errors in a readable
 * format.
 *
 * \param mpi_result The integer error code returned by an MPI function.
 * \return A string containing the human-readable error message corresponding to
 * the error code.
 *
 * \note The returned string is implementation-defined and may vary between
 * different MPI libraries.
 *
 * \throws None. (If MPI_Error_string fails, the returned string may be empty or
 * truncated.)
 */
std::string mpi_error_message(int mpi_result);

/*!
 * \brief Returns a formatted string containing both the MPI error code and its
 * human-readable message.
 *
 * This function combines the integer MPI error code and the corresponding
 * descriptive error message (as returned by mpi_error_message) into a single
 * string. This is useful for logging or debugging, as it provides both the
 * numeric code and its meaning in one place.
 *
 * \param mpi_result The integer error code returned by an MPI function.
 *
 * \return A string in the format "MPI error code: CODE. MPI error message:
 * MESSAGE".
 *
 * \note The error message is implementation-defined and may vary between
 * different MPI libraries.
 *
 * \throws None. (If MPI_Error_string fails, the message part may be empty or
 * truncated.)
 */
std::string mpi_error_and_message(int mpi_result);
}  // namespace rts::detail
