#pragma once

#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <ctime>
#include <locale>
#include <nlohmann/json.hpp>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>

namespace ots::tracker_rates
{
inline std::string graphiteLines(const std::string& text, std::time_t timestamp)
{
	if(text.size() > 16384)
		throw std::runtime_error("Rate summary exceeds 16 KiB");
	const auto j       = nlohmann::json::parse(text);
	auto       require = [](bool ok, const char* message) {
        if(!ok)
            throw std::runtime_error(message);
	};
	auto number = [&](const nlohmann::json& value) {
		require(value.is_number(), "Rate summary value is not numeric");
		const double n = value.get<double>();
		require(std::isfinite(n) && n >= 0, "Rate summary value is negative/nonfinite");
		return n;
	};
	auto integer = [&](const nlohmann::json& value, int low, int high) {
		const double n = number(value);
		require(n == std::floor(n) && n >= low && n <= high,
		        "Invalid rate coverage count");
		return static_cast<int>(n);
	};
	require(j.at("schema_version") == 1, "Unsupported rate summary schema");
	const auto& dtcs = j.at("dtcs");
	require(dtcs.is_array() && !dtcs.empty() && dtcs.size() <= 36,
	        "Invalid DTC coverage");
	const std::regex      uidPattern("^Trk(0[1-9]|1[0-8])_DTC[01]$");
	std::set<std::string> seen;
	std::ostringstream    lines;
	lines.imbue(std::locale::classic());
	lines.precision(17);
	auto emit = [&](const std::string& leaf, double value) {
		lines << "mu2etrk.rates_run." << leaf << ' ' << value << ' ' << timestamp << '\n';
	};
	double sum      = 0;
	int    channels = 0, rocs = 0;
	for(const auto& dtc : dtcs)
	{
		const auto uid = dtc.at("uid").get<std::string>();
		require(std::regex_match(uid, uidPattern) && seen.insert(uid).second,
		        "Invalid/duplicate tracker DTC UID");
		const int nr = integer(dtc.at("rocs"), 1, 6);
		const int nc = integer(dtc.at("channels"), 96, 576);
		require(nc == 96 * nr, "Incomplete 96-channel ROC coverage");
		const double rate = number(dtc.at("total_hz"));
		sum += rate;
		channels += nc;
		rocs += nr;
		emit("dtcs." + uid + ".total_hz", rate);
	}
	const double total = number(j.at("total_hz"));
	require(std::isfinite(sum) && std::abs(sum - total) <= 1e-9 * std::max(1.0, sum),
	        "DTC rates do not sum to tracker total");
	require(integer(j.at("channels"), 96, 20736) == channels,
	        "Tracker coverage does not match DTC coverage");
	emit("total_hz", total);
	emit("channels", channels);
	emit("dtcs", dtcs.size());
	emit("rocs", rocs);
	emit("scan_seconds", number(j.at("scan_seconds")));
	return lines.str();
}

// Only Graphite TCP is accessed; this helper has no DAQ or hardware API.
inline void sendGraphite(const std::string& lines)
{
	struct Address
	{
		addrinfo* p = nullptr;
		~Address()
		{
			if(p)
				freeaddrinfo(p);
		}
	} addresses;
	addrinfo hints{};
	hints.ai_family   = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	const int lookup  = getaddrinfo("mu2e-dcs-01.fnal.gov", "2003", &hints, &addresses.p);
	if(lookup)
		throw std::runtime_error("Graphite hostname lookup failed");
	struct Socket
	{
		int fd = -1;
		~Socket()
		{
			if(fd >= 0)
				close(fd);
		}
	} socket;
	socket.fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if(socket.fd < 0 || fcntl(socket.fd, F_SETFL, O_NONBLOCK) < 0)
		throw std::runtime_error("Cannot create Graphite socket");
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
	auto       writable = [&] {
        for(;;)
        {
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  deadline - std::chrono::steady_clock::now())
                                  .count();
            if(left <= 0)
                throw std::runtime_error("Graphite send timed out; not retried");
            pollfd    p{socket.fd, POLLOUT, 0};
            const int result = poll(&p, 1, static_cast<int>(left));
            if(result < 0 && errno == EINTR)
                continue;
            if(result <= 0 || !(p.revents & POLLOUT))
                throw std::runtime_error("Graphite socket unavailable; not retried");
            return;
        }
	};
	if(connect(socket.fd, addresses.p->ai_addr, addresses.p->ai_addrlen) < 0)
	{
		if(errno != EINPROGRESS)
			throw std::runtime_error("Graphite connection failed");
		writable();
		int       error = 0;
		socklen_t size  = sizeof(error);
		if(getsockopt(socket.fd, SOL_SOCKET, SO_ERROR, &error, &size) < 0 || error)
			throw std::runtime_error("Graphite connection failed");
	}
	size_t offset = 0;
	while(offset < lines.size())
	{
		writable();
		const auto n =
		    send(socket.fd, lines.data() + offset, lines.size() - offset, MSG_NOSIGNAL);
		if(n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
			continue;
		if(n <= 0)
			throw std::runtime_error(
			    "Graphite send failed; delivery uncertain, not retried");
		offset += static_cast<size_t>(n);
	}
}
}  // namespace ots::tracker_rates
