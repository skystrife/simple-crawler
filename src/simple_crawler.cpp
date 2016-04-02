/**
 * A simplistic crawling application. Starts from a seed URL and expands to
 * links reachable from there, optionally matching a regex pattern.
 *
 * This uses the cpr library for handling page fetching using cURL,
 * Google's Gumbo parser for parsing the documents, and gumbo-libxml for
 * providing a libxml compatible API to be used for extracting links.
 *
 * The first argument to this application should be a configuration file in
 * TOML format, which will specify the seed URL, the URL regular expression
 * to match, and other various settings for the crawler.
 *
 * @file simple_crawler.cpp
 * @author Chase Geigle
 */

#include <chrono>
#include <iostream>
#include <queue>
#include <regex>
#include <string>
#include <unordered_set>

#include "gumbo_libxml.h"
#include "libxml/tree.h"
#include "libxml/xpath.h"
#include "cpr/cpr.h"
#include "cpptoml.h"

void block_urls(std::istringstream& input, const std::string& protocol,
                const std::string& domain,
                std::unordered_set<std::string>& blocked)
{
    std::string line;
    while (std::getline(input, line))
    {
        if (line == "User-agent: *")
            break;
    }

    std::string disallow = "Disallow: ";
    while (std::getline(input, line))
    {
        auto ua_pos = line.find("User-agent: ");
        if (ua_pos != std::string::npos && line.back() != '*')
            break;

        auto pos = line.find("Disallow: ");
        if (pos == std::string::npos)
            continue;

        auto url
            = protocol + "://" + domain + line.substr(pos + disallow.length());
        blocked.insert(url);
    }
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "Usage: " << argv[0] << " config.toml" << std::endl;
        return 1;
    }

    auto config = cpptoml::parse_file(argv[1]);

    auto seed_url = *config->get_as<std::string>("seed-url");

    auto proto_pos = seed_url.find("://");
    if (proto_pos == std::string::npos)
    {
        std::cerr << "Couldn't figure out protocol in seed url: " << seed_url
                  << std::endl;
        return 0;
    }
    auto protocol = seed_url.substr(0, proto_pos);

    auto domain_start = proto_pos + 3;
    auto domain_end = seed_url.find("/", domain_start);
    if (domain_end == std::string::npos)
    {
        std::cerr << "Couldn't figure out domain in seed url: " << seed_url
                  << std::endl;
        return 0;
    }
    auto domain = seed_url.substr(domain_start, domain_end - domain_start);

    std::unordered_set<std::string> visited;
    std::cout << "Obtaining robots.txt..." << std::endl;

    auto resp_robots
        = cpr::Get(cpr::Url{protocol + "://" + domain + "/robots.txt"});

    std::istringstream robots{resp_robots.text};
    block_urls(robots, protocol, domain, visited);

    std::cout << "Blocked " << visited.size() << " urls..." << std::endl;

    std::regex url_regex{*config->get_as<std::string>("url-regex")};
    auto sleep_time = *config->get_as<int64_t>("sleep-time");

    auto save_html = config->get_as<bool>("save-html").value_or(false);
    auto save_text = config->get_as<bool>("save-text").value_or(false);

    if (!save_html && !save_text)
    {
        std::cerr << "No saving settings present in " << argv[1] << std::endl;
        return 1;
    }

    std::queue<std::string> to_visit;
    to_visit.push(seed_url);
    visited.insert(seed_url);

    while (!to_visit.empty())
    {
        auto url = to_visit.front();
        to_visit.pop();

        std::cout << url << " -> " << std::flush;

        auto response = cpr::Get(cpr::Url{url});
        auto response_time = std::chrono::steady_clock::now();
        std::cout << response.status_code;

        if (response.status_code != 200)
        {
            std::cout << " (error!)" << std::flush;
        }
        else
        {
            if (response.header["Content-Type"].find("text/html")
                == std::string::npos)
            {
                std::cout << " (skipped; non-html)" << std::flush;
            }
            else
            {
                auto start_pos = url.find_last_of("/");
                auto filename = url.substr(start_pos + 1);
                if (save_html)
                {
                    std::ofstream output{"html/" + filename + ".html"};
                    output << response.text;
                }

                xmlDocPtr doc = gumbo_libxml_parse(response.text.c_str());
                xmlXPathContextPtr xpath_ctx = xmlXPathNewContext(doc);

                auto text_expr = reinterpret_cast<const xmlChar*>(
                    "//text()[normalize-space() and not(ancestor::script | "
                    "ancestor::style)]");

                xmlXPathObjectPtr xpath_obj
                    = xmlXPathEvalExpression(text_expr, xpath_ctx);

                if (save_text)
                {
                    std::ofstream output{"text/" + filename + ".txt"};
                    for (int i = 0; i < xpath_obj->nodesetval->nodeNr; ++i)
                    {
                        xmlNodePtr ptr = xpath_obj->nodesetval->nodeTab[i];
                        output << ptr->content << " ";
                    }
                }

                // extract links
                auto a_expr = reinterpret_cast<const xmlChar*>("//a/@href");
                xmlXPathObjectPtr xpath_obj_a
                    = xmlXPathEvalExpression(a_expr, xpath_ctx);

                uint64_t num_added = 0;
                for (int i = 0; i < xpath_obj_a->nodesetval->nodeNr; ++i)
                {
                    auto ptr = xpath_obj_a->nodesetval->nodeTab[i];
                    auto href_attr = ptr->children->content;

                    if (!href_attr)
                        continue;

                    std::string href = reinterpret_cast<const char*>(href_attr);

                    if (href[0] == '#')
                        continue;

                    if (href[0] == '/')
                    {
                        if (href.size() > 1 && href[1] == '/')
                        {
                            href = protocol + ":" + href;
                        }
                        else
                        {
                            href = protocol + "://" + domain + href;
                        }
                    }

                    href = href.substr(0, href.find_last_of("#/"));

                    if (std::regex_search(href, url_regex)
                        && visited.find(href) == visited.end())
                    {
                        ++num_added;
                        visited.insert(href);
                        to_visit.push(href);
                    }
                }

                std::cout << " (" << num_added << " new links, "
                          << to_visit.size() << " total)";

                xmlXPathFreeObject(xpath_obj_a);
                xmlXPathFreeObject(xpath_obj);
                xmlXPathFreeContext(xpath_ctx);
                xmlFreeDoc(doc);
            }
        }

        std::this_thread::sleep_until(response_time
                                      + std::chrono::milliseconds{sleep_time});
        std::cout << std::endl;
    }

    return 0;
}
