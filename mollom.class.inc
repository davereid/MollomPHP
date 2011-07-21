<?php

/**
 * @file
 * Mollom client class.
 *
 * @todo Refine exceptions by necessity of having to react differently.
 *   - All 5xx HTTP codes, MollomRedirectException, and MollomResponseException
 *     should make the client try the next server.
 *   - Only AUTH_ERROR and 404 are not recoverable.
 * @todo Doing a "verifyKey" with an empty server list leads to two/duplicate
 *   and unnecessary GET site/$id requests, one for the server list, subsequent
 *   ones for whatever/verifyKey. Some resources can be cached?
 */

/**
 * A catchable Mollom exception.
 *
 * The Mollom class internally uses exceptions to handle client-side server
 * balancing and eventually HTTP request errors within the Mollom::request()
 * method. All exceptions thrown in the Mollom class and derived classes should
 * be instances of the MollomException class if they pertain to errors that can
 * be catched/handled within the class. Other errors should not use the
 * MollomException class and handled differently.
 *
 * No MollomException is supposed to pile up as a user-facing fatal error. All
 * functions that invoke Mollom::request() have to catch Mollom exceptions.
 *
 * @see Mollom::query()
 * @see Mollom::request()
 *
 * @param $message
 *   The Exception message to throw.
 * @param $code
 *   The Exception code.
 * @param $previous
 *   (optional) The previous Exception, if any.
 * @param $instance
 *   The Mollom class instance the Exception is thrown in.
 * @param $arguments
 *   (optional) A associative array containing information about a performed
 *   HTTP request that failed:
 *   - request: (string) The HTTP method and URI of the performed request; e.g.,
 *     "GET http://server.mollom.com/v1/foo/bar". In case of GET requests, do
 *     not add query parameters to the URI; pass them in 'data' instead.
 *   - data: (array) An associative array containing HTTP GET/POST/PUT request
 *     query parameters that were sent to the server.
 *   - response: (mixed) The server response, either as string, or the already
 *     parsed response; i.e., an array.
 */
class MollomException extends Exception {
  /**
   * @var Mollom
   */
  protected $mollom;

  /**
   * The severity of this exception.
   *
   * @var string
   */
  protected $severity = 'debug';

  public $arguments = array();

  function __construct($message = '', $code = 0, Exception $previous = NULL, Mollom $mollom, array $arguments = array()) {
    // Fatal error on PHP <5.3 when passing more arguments to Exception.
    if (version_compare(phpversion(), '5.3') >= 0) {
      parent::__construct($message, $code, $previous);
    }
    else {
      parent::__construct($message, $code);
    }

    // Set the error code on the Mollom class.
    $mollom->lastResponseCode = $code;

    // Log the exception.
    $message = array(
      'type' => $this->severity,
      'message' => 'Error @code: %message',
      'arguments' => array(
        '@code' => $code,
        '%message' => $message,
      ),
    );
    // Add HTTP request information, if available.
    if (!empty($arguments)) {
      $message += array(
        'request' => $arguments['request'],
        'data' => $arguments['data'],
        'response' => $arguments['response'],
      );
    }
    $mollom->log[] = $message;

    // @todo Remove?
    $this->setArguments($arguments);
  }

  public function setArguments($arguments) {
    $this->arguments = $arguments;
    return $this;
  }

  public function getArguments() {
    return $this->arguments;
  }
}

/**
 * Mollom network error exception.
 *
 * Thrown in case a HTTP request results in code <= 0, denoting a low-level
 * communication error.
 *
 * @todo Not actively caught anywhere yet.
 */
class MollomNetworkException extends MollomException {
  protected $severity = 'error';
}

/**
 * Mollom authentication error exception.
 *
 * Thrown in case API keys or other authentication parameters are invalid.
 */
class MollomAuthenticationException extends MollomException {
  protected $severity = 'error';
}

/**
 * Mollom server refresh exception.
 *
 * Thrown when a Mollom server asks the client to update the server list.
 */
class MollomRefreshException extends MollomException {
  protected $severity = 'debug';
}

/**
 * Mollom server redirect exception.
 *
 * Thrown when a Mollom server asks the client to use next server in the server
 * list.
 */
class MollomRedirectException extends MollomException {
  protected $severity = 'debug';
}

/**
 * Mollom server response exception.
 *
 * Thrown when a request to a Mollom server succeeds, but the response does not
 * contain an expected element; e.g., a backend configuration or execution
 * error that possibly exists on one server only.
 *
 * @see Mollom::request()
 */
class MollomResponseException extends MollomException {
  protected $severity = 'debug';
}

/**
 * The base class for Mollom client implementations.
 */
abstract class Mollom {
  /**
   * The Mollom API version, used in HTTP requests.
   */
  const API_VERSION = 'v1';

  /**
   * Network communication failure code: No servers could be reached.
   *
   * @see MollomNetworkException
   */
  const NETWORK_ERROR = 900;

  /**
   * Server communication failure code: Unexpected server response.
   *
   * Using the 5xx HTTP status code range, but not re-using an existing HTTP
   * code to prevent bogus bug reports. 511 is the closest comparable code
   * 501 (Not Implemented) plus 10.
   *
   * @see MollomResponseException
   */
  const RESPONSE_ERROR = 511;

  /**
   * Server communication failure code: Authentication error.
   *
   * @see MollomAuthenticationException
   */
  const AUTH_ERROR = 1000;

  /**
   * Server communication failure code: Client is asked to update the server list.
   *
   * @see MollomRefreshException
   */
  const REFRESH_ERROR = 1100;

  /**
   * Server communication failure code: Client is asked to try next server.
   *
   * @see MollomRedirectException
   */
  const REDIRECT_ERROR = 1200;

  /**
   * The public Mollom API key to use for request authentication.
   *
   * @var string
   */
  public $keyPublic = '';

  /**
   * The private Mollom API key to use for request authentication.
   *
   * @var string
   */
  public $keyPrivate = '';

  /**
   * The list of Mollom servers to communicate with, as returned by Mollom.
   */
  public $servers = array();

  /**
   * A hard-coded list of Mollom servers to fetch the server list from.
   */
  public $serversInit = array('http://megatron.mollom.com');

  /**
   * The status code of the last response or TRUE if it succeeded.
   */
  public $lastResponseCode = NULL;

  /**
   * A list of logged requests.
   */
  public $log = array();

  function __construct() {
    $this->keyPublic = $this->loadConfiguration('publicKey');
    $this->keyPrivate = $this->loadConfiguration('privateKey');
  }

  /**
   * Loads a configuration value from client-side storage.
   *
   * @param string $name
   *   The configuration setting name to load, one of:
   *   - publicKey: The public API key for Mollom authentication.
   *   - privateKey: The private API key for Mollom authentication.
   *   - servers: An indexed array of Mollom server addresses.
   *
   * @return mixed
   *   The stored configuration value or NULL if there is none.
   *
   * @see Mollom::saveConfiguration()
   * @see Mollom::deleteConfiguration()
   */
  abstract public function loadConfiguration($name);

  /**
   * Saves a configuration value to client-side storage.
   *
   * @param string $name
   *   The configuration setting name to save.
   * @param mixed $value
   *   The value to save.
   *
   * @see Mollom::loadConfiguration()
   * @see Mollom::deleteConfiguration()
   */
  abstract public function saveConfiguration($name, $value);

  /**
   * Deletes a configuration value from client-side storage.
   *
   * @param string $name
   *   The configuration setting name to delete.
   *
   * @see Mollom::loadConfiguration()
   * @see Mollom::saveConfiguration()
   */
  abstract public function deleteConfiguration($name);

  /**
   * Returns platform and version information about the Mollom client.
   *
   * Retrieves platform and Mollom client version information to send along to
   * Mollom when verifying keys.
   *
   * This information is used to speed up support requests and technical
   * inquiries. The data may also be aggregated to help the Mollom staff to make
   * decisions on new features or the necessity of back-porting improved
   * functionality to older versions.
   *
   * @return
   *   An associative array containing:
   *   - platformName: The name of the platform/distribution; e.g., "Drupal".
   *   - platformVersion: The version of platform/distribution; e.g., "7.0".
   *   - clientName: The official Mollom client name; e.g., "Mollom".
   *   - clientVersion: The version of the Mollom client; e.g., "7.x-1.0".
   */
  abstract public function getClientInformation();

  /**
   * Writes log messages to a permanent location/storage.
   *
   * Not abstract, since clients are not required to write log messages.
   * However, all clients should permanently store the log messages, as it
   * dramatically improves resolution of support requests filed by users.
   * The log may be written and appended to a file (via file_put_contents()),
   * syslog (on *nix-based systems), or a database.
   *
   * @see Mollom::log
   */
  public function writeLog() {
    // After writing log messages, empty the log.
    $this->purgeLog();
  }

  /**
   * Purges captured log messages.
   *
   * @see Mollom::writeLog()
   */
  final public function purgeLog() {
    $this->log = array();
  }

  /**
   * Returns the current request time as UNIX timestamp.
   */
  public function getRequestTime() {
    return $_SERVER['REQUEST_TIME'];
  }

  /**
   * Generates authentication parameters for communication with Mollom servers.
   *
   * This function generates an array with all information required to
   * authenticate against Mollom. To prevent forged requests where you are
   * impersonated, each request is signed with a hash based on a private
   * key and a timestamp.
   *
   * Both the client and the server share the secret key used to create
   * the authentication hash. They both hash a timestamp with the secret
   * key, and if the hashes match, the authenticity of the message is
   * validated.
   *
   * To avoid someone intercepting a (hash, timestamp)-pair and using it
   * to impersonate a client, Mollom rejects any request where the timestamp
   * is more than 15 minutes off.
   *
   * Make sure your server's time is synchronized with the world clocks,
   * and that you don't share your private key with anyone else.
   */
  public function getAuthentication() {
    if (empty($this->keyPublic) || empty($this->keyPrivate)) {
      throw new MollomAuthenticationException('Missing API keys.', self::AUTH_ERROR, NULL, $this);
    }

    // Generate a timestamp according to the dateTime format.
    // @see http://www.w3.org/TR/xmlschema-2/#dateTime
    $time = gmdate("Y-m-d\TH:i:s.\\0\\0\\0O", $this->getRequestTime());

    // Generate a random number.
    $nonce = md5(mt_rand());

    // Calculate a HMAC-SHA1 according to RFC 2104.
    // @see http://www.ietf.org/rfc/rfc2104.txt
    $request_data = $time . ':' . $nonce . ':' . $this->keyPrivate;
    $hash = base64_encode(hash_hmac('sha1', $request_data, $this->keyPrivate, TRUE));

    // Return HMAC authentication parameters as a keyed array.
    $data['publicKey'] = $this->keyPublic;
    $data['time'] = $time;
    $data['hash'] = $hash;
    $data['nonce'] = $nonce;

    return $data;
  }

  /**
   * Fetches Mollom servers from local configuration or retrieves a new list.
   *
   * @see Mollom::refreshServers()
   */
  public function getServers() {
    // If there is no server list yet, consult the local configuration.
    if (empty($this->servers)) {
      $servers = $this->loadConfiguration('servers');
      // Use the local configuration value, if any.
      if (!empty($servers) && is_array($servers)) {
        $this->servers = $servers;
      }
      // Otherwise, retrieve a new server list from Mollom.
      else {
        $this->servers = $this->refreshServers();
        if ($this->servers) {
          $this->saveConfiguration('servers', $this->servers);
          $this->log[] = array(
            'type' => 'debug',
            'message' => 'Refreshed servers: %servers',
            'arguments' => array(
              '%servers' => implode(', ', $this->servers),
            ),
          );
        }
      }
    }
    return $this->servers;
  }

  /**
   * Returns a new server list retrieved from Mollom.
   *
   * The server list returned from Mollom should be stored as configuration
   * value on the client-side. This method should only be called when the
   * configuration value is not yet or no longer available, or when a Mollom
   * server specifically asks the client to refresh its server list.
   *
   * @see Mollom::getServers()
   * @see Mollom::query()
   * @see MollomRefreshException
   * @see Mollom::REFRESH_ERROR
   */
  protected function refreshServers() {
    // refreshServers() cannot use query() as we need to prevent infinite
    // recursion. In addition, we handle returned error codes differently here,
    // since REDIRECT_ERROR, REFRESH_ERROR, and any other communication error
    // requires us to skip to the next server in order to retrieve a new server
    // list. We only ever abort, if we get an AUTH_ERROR, in which case there
    // is a configuration error (i.e., invalid API keys).
    $servers = array();
    try {
      $data = $this->getAuthentication();
    }
    catch (MollomAuthenticationException $e) {
      return $servers;
    }

    $path = 'site/' . $this->keyPublic;
    $expected = array('site', 'servers');
    foreach ($this->serversInit as $server) {
      try {
        $result = $this->request('GET', $server . '/' . self::API_VERSION, $path, $data, $expected);
      }
      catch (MollomAuthenticationException $e) {
        // Bogus configuration. Stop trying, since all servers will fail.
        break;
      }
      catch (MollomException $e) {
        // On any other error, skip to the next server.
        continue;
      }

      if (isset($result) && $this->lastResponseCode === TRUE) {
        $servers = $result['site']['servers'];
        break;
      }
    }
    return $servers;
  }

  /**
   * Retrieve or send data from/to Mollom servers.
   *
   * @param string $method
   *   The HTTP method to use; i.e., 'GET', 'POST', or 'PUT'.
   * @param string $path
   *   The REST path/resource to request; e.g., 'site/1a2b3c'.
   * @param array $data
   *   An associative array of query parameters to send with the request.
   * @param array $expected
   *   (optional) An element that is expected in the response, denoted as a list
   *   of parent element keys to the element and the element key itself; e.g., a
   *   value of array('site', 'servers') expects $response['site']['servers'] to
   *   exist in the response.
   *
   * @see Mollom::request()
   */
  protected function query($method, $path, array $data = array(), array $expected = array()) {
    // Retrieve server list.
    // If we get no list, we will have no servers to iterate over and only the
    // error logic remains.
    $this->getServers();

    // Unconditionally add HMAC request authentication parameters.
    try {
      $data += $this->getAuthentication();
    }
    catch (MollomAuthenticationException $e) {
      // MollomAuthenticationException sets the appropriate error code. Merely
      // catch the exception, move on to error logic below.
      // Prevent any requests from being performed in case we have servers
      // loaded from configuration or due to some other edge-case (scripting).
      $this->servers = array();
    }

    // Initialize refresh variable.
    $refresh = FALSE;
    // Send the request to the first server; if that fails, try the other
    // servers in the list.
    // @todo The Mollom instance "sticks" now, as it's statically cached. Thus,
    //   the array cursor in $this->servers is retained across multiple queries.
    //   I.e., subsequent queries will continue to communicate with the
    //   "current" server, which might not be the first.
    //   PRO: In a scenario that uses the statically cached class instance
    //   (low-level scripts and possibly subsequent form submissions in the
    //   future) and in which a server redirects, subsequent queries will
    //   continue to use the current/second/next server in the list instead of
    //   restarting on the first; i.e., potentially less "wasted" requests.
    //   CON: Higher chance to reach the end of the server list, unless we allow
    //   to iterate two times over the server list until we consider a request
    //   to fail.
    while ($server = current($this->servers)) {
      try {
        $result = $this->request($method, $server, $path, $data, $expected);
      }
      catch (MollomRefreshException $e) {
        // Prevent infinite loops.
        if (!$refresh) {
          $refresh = TRUE;

          // In any case, the current server list is no longer valid.
          $this->servers = array();
          $this->deleteConfiguration('servers');

          // Retrieve a fresh list of Mollom servers.
          $this->servers = $this->getServers();
          // If API keys are invalid, we won't be able to get a new server list.
          // To reach this, we must have had a server list (and therefore
          // valid keys) before, so we do not immediately return, but trigger
          // the fallback mode instead.
          if (empty($this->servers)) {
            break;
          }
        }
      }
      catch (MollomRedirectException $e) {
        // Try the next server in the list.
        $next = next($this->servers);

        // @todo $next may be FALSE, confusing users looking into logs.
        $this->log[] = array(
          'type' => 'debug',
          'message' => 'Server %server redirected to %next.',
          'arguments' => array(
            '%server' => $server,
            '%next' => $next,
          ),
        );
        continue;
      }
      catch (MollomAuthenticationException $e) {
        // This is an irrecoverable error, so don't try other servers.
        break;
      }
      catch (MollomException $e) {
        // If the resource does not exist, there is no point in trying another
        // server.
        // @todo Error handling?
        if ($e->getCode() == 404) {
          break;
        }
        // On any other known error, try the next server.
        next($this->servers);
        continue;
      }

      // Unless we have a positive result, continue to next server.
      // @todo Handle uncaught exceptions and errors?
      if ($this->lastResponseCode === TRUE) {
        break;
      }
      else {
        next($this->servers);
      }
    }

    // In case all servers failed, reset the server list to enforce retrieval of
    // a new list the next time.
    if (current($this->servers) === FALSE) {
      $this->servers = array();
      $this->deleteConfiguration('servers');

      $this->log[] = array(
        'type' => 'error',
        'message' => 'All servers unreachable or returning errors. The server list was emptied: %servers',
        'arguments' => array(
          '%servers' => implode(', ', $this->servers ? $this->servers : $this->serversInit),
        ),
      );
    }

    // Write all captured log messages.
    $this->writeLog();

    // If there is a result (only possible with a server list) and the last
    // request succeeded, return the result to the caller.
    if (isset($result) && $this->lastResponseCode === TRUE) {
      return $result;
    }
    // If the last request succeeded but there was a unexpected response, return
    // the error code.
    if ($this->lastResponseCode == self::RESPONSE_ERROR) {
      return $this->lastResponseCode;
    }
    // Return an authentication error, which may require special client-side
    // processing.
    if ($this->lastResponseCode == self::AUTH_ERROR) {
      return $this->lastResponseCode;
    }
    // Return a not found error, which always needs to be handled by the calling
    // code.
    if ($this->lastResponseCode == 404) {
      return $this->lastResponseCode;
    }

    // In case of any kind of HTTP error (404, 0 [invalid-address],
    // -1002 [bad URI], etc), return a generic NETWORK_ERROR.
    return self::NETWORK_ERROR;
  }

  /**
   * Performs a HTTP request to a Mollom server.
   *
   * @param string $method
   *   The HTTP method to use; i.e., 'GET', 'POST', or 'PUT'.
   * @param string $server
   *   The base URL of the server to perform the request against; e.g.,
   *   'http://foo.mollom.com'.
   * @param string $path
   *   The REST path/resource to request; e.g., 'site/1a2b3c'.
   * @param array $data
   *   An associative array of query parameters to send with the request.
   * @param array $expected
   *   (optional) An element that is expected in the response, denoted as a list
   *   of parent element keys to the element and the element key itself; e.g., a
   *   value of array('site', 'servers') expects $response['site']['servers'] to
   *   exist in the response.
   *
   * @throws MollomNetworkException
   * @throws MollomAuthenticationException
   * @throws MollomRedirectException
   * @throws MollomRefreshException
   * @throws MollomException
   *
   * @see Mollom::query()
   * @see Mollom::httpBuildQuery()
   * @see Mollom::httpParseQuery()
   * @see Mollom::parseXML()
   * @see json_decode()
   */
  abstract protected function request($method, $server, $path, array $data, array $expected = array());

  /**
   * Converts a SimpleXMLIterator structure into an associative array.
   *
   * Used to parse an XML response from Mollom servers into a PHP array. For
   * example:
   * @code
   * $elements = new SimpleXmlIterator($response_body);
   * $parsed_response = $this->parseXML($elements);
   * @endcode
   *
   * @param $sxi
   *   A SimpleXMLIterator structure of the server response body.
   *
   * @return array
   *   An associative, possibly multidimensional array.
   */
  public static function parseXML(SimpleXMLIterator $sxi) {
    $a = array();
    $remove = array();
    for ($sxi->rewind(); $sxi->valid(); $sxi->next()) {
      $key = $sxi->key();

      // Recurse into non-scalar values.
      if ($sxi->hasChildren()) {
        $value = self::parseXML($sxi->current());
      }
      // Use a simple key/value pair for scalar values.
      else {
        $value = strval($sxi->current());
      }

      if (!isset($a[$key])) {
        $a[$key] = $value;
      }
      // Convert already existing keys into indexed keys, retaining other
      // existing keys in the array; i.e., two or more XML elements of the
      // same name and on the same level.
      // Note that this XML to PHP array conversion does not support multiple
      // different elements that each appear multiple times.
      else {
        // First time we reach here, convert the existing keyed item. Do not
        // remove $key, so we enter this path again.
        if (!isset($remove[$key])) {
          $a[] = $a[$key];
          // Mark $key for removal.
          $remove[$key] = $key;
        }
        // Add the new item.
        $a[] = $value;
      }
    }
    // Lastly, remove named keys that have been converted to indexed keys.
    foreach ($remove as $key) {
      unset($a[$key]);
    }
    return $a;
  }

  /**
   * Determines whether a nested array with variable depth contains all of the requested keys.
   *
   * @param array $array
   *   The array with variable depth that may contain the value to check for.
   * @param array $expected
   *   A list of parent keys of the value, starting with the outermost key.
   *
   * @return
   *   TRUE if all the parent keys exist, FALSE otherwise.
   *
   * @see Mollom::request()
   */
  public static function arrayKeyExists(array $array, array $expected) {
    $ref = &$array;
    foreach ($expected as $parent) {
      if (is_array($ref) && array_key_exists($parent, $ref)) {
        $ref = &$ref[$parent];
      }
      else {
        return FALSE;
      }
    }
    return TRUE;
  }

  /**
   * Builds an RFC-compliant, rawurlencoded query string.
   *
   * PHP did a design decision to only support HTTP query parameters in the form
   * of foo[]=1&foo[]=2, primarily for its built-in and automated conversion to
   * PHP arrays. Other platforms (including the Mollom backend) do not support
   * this syntax and expect multiple parameters to be in the form of
   * foo=1&foo=2.
   *
   * @see http_build_query()
   * @see http://en.wikipedia.org/wiki/Query_string
   * @see http://tools.ietf.org/html/rfc3986#section-3.4
   *
   * @param array $query
   *   The query parameter array to be processed, e.g. $_GET.
   * @param string $parent
   *   Internal use only. Used to build the $query array key for nested items.
   *
   * @return array
   *   A rawurlencoded string which can be used as or appended to the URL query
   *   string.
   *
   * @see Mollom::httpParseQuery()
   */
  public static function httpBuildQuery(array $query, $parent = '') {
    $params = array();

    foreach ($query as $key => $value) {
      // For indexed (unnamed) child array keys, use the same parameter name,
      // leading to param=foo&param=bar instead of param[]=foo&param[]=bar.
      if ($parent && is_int($key)) {
        $key = rawurlencode($parent);
      }
      else {
        $key = ($parent ? $parent . '[' . rawurlencode($key) . ']' : rawurlencode($key));
      }

      // Recurse into children.
      if (is_array($value)) {
        $params[] = self::httpBuildQuery($value, $key);
      }
      // If a query parameter value is NULL, only append its key.
      elseif (!isset($value)) {
        $params[] = $key;
      }
      else {
        // For better readability of paths in query strings, we decode slashes.
        $params[] = $key . '=' . str_replace('%2F', '/', rawurlencode($value));
      }
    }

    return implode('&', $params);
  }

  /**
   * Parses an RFC-compliant, rawurlencoded query string.
   *
   * Mollom clients normally do not need this function, as they do not need to
   * process requests from a server - unless a client attempts to implement
   * client-side unit testing.
   *
   * @param string $query
   *   The query parameter string to process, e.g. $_SERVER['QUERY_STRING']
   *   (GET) or php://input (POST/PUT).
   *
   * @return array
   *   A query parameter array parsed from $query.
   *
   * @see Mollom::httpBuildQuery()
   * @see parse_str()
   */
  public static function httpParseQuery($query) {
    if ($query === '') {
      return array();
    }
    // Explode parameters into arrays to check for duplicate names.
    $params = array();
    $seen = array();
    $duplicate = array();
    foreach (explode('&', $query) as $chunk) {
      $param = explode('=', $chunk, 2);
      if (isset($seen[$param[0]])) {
        $duplicate[$param[0]] = TRUE;
      }
      $seen[$param[0]] = TRUE;
      $params[] = $param;
    }
    // Implode back into a string.
    $query = '';
    foreach ($params as $param) {
      $query .= $param[0];
      if (isset($duplicate[$param[0]])) {
        $query .= '[]';
      }
      if (isset($param[1])) {
        $query .= '=' . $param[1];
      }
      $query .= '&';
    }
    // Parse query string as usual.
    parse_str($query, $result);
    return $result;
  }

  public function getSites() {
    $result = $this->query('GET', 'site', array(), array('list'));
    // In XML, 'list' is a string when blacklist is empty.
    // @todo Move into query().
    if (isset($result['list'])) {
      // parseXML() can only convert multiple sub-elements into an indexed array.
      if (is_array($result['list'])) {
        $result['list'] = array_values($result['list']);
        return $result['list'];
      }
      return array();
    }
    return $result;
  }

  public function getSite($siteId = NULL) {
    if (!isset($siteId)) {
      $siteId = $this->keyPublic;
    }
    $result = $this->query('GET', 'site/' . $siteId, array(), array('site'));
    return isset($result['site']) ? $result['site'] : $result;
  }

  public function createSite($data = array()) {
    $result = $this->query('POST', 'site', $data, array('site'));
    return isset($result['site']) ? $result['site'] : $result;
  }

  public function verifyKey() {
    $data = $this->getClientInformation();
    $result = $this->query('POST', 'site/' . $this->keyPublic, $data, array('site'));
    // If the public key could not be found, make sure to return an
    // authentication error.
    if ($this->lastResponseCode === 404) {
      return self::AUTH_ERROR;
    }
    // lastResponseCode will either be TRUE, AUTH_ERROR, or NETWORK_ERROR.
    return $this->lastResponseCode === TRUE ? TRUE : $this->lastResponseCode;
  }

  public function deleteSite($siteId) {
    $result = $this->query('POST', 'site/' . $siteId . '/delete');
    return $this->lastResponseCode === TRUE;
  }

  public function checkContent($data = array()) {
    $path = 'content';
    if (!empty($data['id'])) {
      $path .= '/' . $data['id'];
    }
    $result = $this->query('POST', $path, $data, array('content', 'id'));

    // parseXML() can only convert multiple sub-elements into an indexed array.
    if (isset($result['content']['languages']) && is_array($result['content']['languages'])) {
      $result['content']['languages'] = array_values($result['content']['languages']);
    }

    return isset($result['content']) ? $result['content'] : $result;
  }

  public function getCaptcha($data = array()) {
    if (!isset($data['type']) || !in_array($data['type'], array('image', 'audio'))) {
      // @todo Error handling?
      throw new MollomException('Unknown CAPTCHA type.', 0, NULL, $this);
    }
    $path = 'captcha';
    if (!empty($data['id'])) {
      $path .= '/' . $data['id'];
    }
    $result = $this->query('POST', $path, $data, array('captcha', 'id'));

    return isset($result['captcha']) ? $result['captcha'] : $result;
  }

  public function checkCaptcha($data = array()) {
    if (empty($data['id'])) {
      // @todo Error handling?
      throw new MollomException('Missing CAPTCHA ID.', 0, NULL, $this);
    }
    $result = $this->query('POST', 'captcha/' . $data['id'], $data, array('captcha', 'id'));

    return isset($result['captcha']) ? $result['captcha'] : $result;
  }

  /**
   * @todo This part of the REST API needs work. No way to report CAPTCHA sessions.
   */
  public function sendFeedback($data = array()) {
    if (empty($data['contentId']) && empty($data['captchaId'])) {
      // @todo Error handling?
      throw new MollomException('Missing resource ID.', 0, NULL, $this);
    }
    if (empty($data['reason'])) {
      // @todo Error handling?
      throw new MollomException('Missing feedback reason.', 0, NULL, $this);
    }
    $this->query('POST', 'feedback', $data);
    return $this->lastResponseCode === TRUE ? TRUE : FALSE;
  }

  /**
   * @todo List parameters.
   */
  public function getBlacklist($siteId = NULL) {
    if (!isset($siteId)) {
      $siteId = $this->keyPublic;
    }
    $result = $this->query('GET', 'blacklist/' . $siteId, array(), array('list'));
    // In XML, 'list' is a string when blacklist is empty.
    // @todo Move into query().
    if (isset($result['list'])) {
      // parseXML() can only convert multiple sub-elements into an indexed array.
      if (is_array($result['list'])) {
        $result['list'] = array_values($result['list']);
        return $result['list'];
      }
      return array();
    }
    return $result;
  }

  public function getBlacklistEntry($entryId, $siteId = NULL) {
    if (!isset($siteId)) {
      $siteId = $this->keyPublic;
    }
    $result = $this->query('GET', 'blacklist/' . $siteId . '/' . $entryId, array(), array('entry', 'id'));
    return isset($result['entry']) ? $result['entry'] : $result;
  }

  public function createBlacklistEntry($data = array(), $siteId = NULL) {
    if (!isset($siteId)) {
      $siteId = $this->keyPublic;
    }
    $result = $this->query('POST', 'blacklist/' . $siteId, $data, array('entry', 'id'));
    return isset($result['entry']) ? $result['entry'] : $result;
  }

  public function updateBlacklistEntry($data = array(), $siteId = NULL) {
    if (empty($data['id'])) {
      // @todo Error handling?
      throw new MollomException('Missing blacklist entry ID.', 0, NULL, $this);
    }
    if (!isset($siteId)) {
      $siteId = $this->keyPublic;
    }
    $result = $this->query('POST', 'blacklist/' . $siteId . '/' . $data['id'], $data, array('entry', 'id'));

    return isset($result['entry']) ? $result['entry'] : $result;
  }

  public function deleteBlacklistEntry($entryId, $siteId = NULL) {
    if (!isset($siteId)) {
      $siteId = $this->keyPublic;
    }
    $result = $this->query('POST', 'blacklist/' . $siteId . '/' . $entryId . '/delete');
    return $this->lastResponseCode === TRUE;
  }
}

