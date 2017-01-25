require 'minitest/autorun'
require 'fileutils'
require 'byebug'
require File.expand_path(File.join(File.dirname(__FILE__), 'net_http_multipart_post'))
require File.expand_path(File.join(File.dirname(__FILE__), 'verm_spawner'))

verm_binary = File.join(File.dirname(__FILE__), '..', 'verm')
verm_data   = File.join(File.dirname(__FILE__), 'data')
mime_types_file = File.join(File.dirname(__FILE__), 'fixtures', 'mime.types')
captured_stdout_filename = File.join(File.dirname(__FILE__), 'tmp', 'captured_stdout') unless ENV['NO_CAPTURE_STDOUT'].to_i > 0
captured_stderr_filename = File.join(File.dirname(__FILE__), 'tmp', 'captured_stderr') unless ENV['NO_CAPTURE_STDERR'].to_i > 0
FileUtils.mkdir_p(File.join(File.dirname(__FILE__), 'tmp'))
VERM_SPAWNER = VermSpawner.new(verm_binary, verm_data, :mime_types_file => mime_types_file, :capture_stdout_in => captured_stdout_filename, :capture_stderr_in => captured_stderr_filename)
REPLICATION_MASTER_VERM_SPAWNER = VermSpawner.new(verm_binary, "#{verm_data}_replica", :mime_types_file => mime_types_file, :port => VERM_SPAWNER.port + 1, :replicate_to => VERM_SPAWNER.host, :capture_stdout_in => captured_stdout_filename, :capture_stderr_in => captured_stderr_filename)

module Verm
  class TestCase < Minitest::Test
    undef_method :default_test if instance_methods.include? 'default_test' or
                                  instance_methods.include? :default_test

    def extra_spawner_options
      {}
    end
  
    def setup
      @multipart = nil
      VERM_SPAWNER.setup(extra_spawner_options)
    end

    def teardown
      VERM_SPAWNER.teardown
    end
    
    def timeout
      10 # seconds
    end

    def repeatedly_wait_until
      (timeout*10).times do
        return if yield
        sleep 0.1
      end
      raise TimeoutError
    end
    

    def gzip(str)
      output = StringIO.new("".force_encoding("binary"))
      gz = Zlib::GzipWriter.new(output)
      gz.write(str)
      gz.close
      output.string
    end

    def ungzip(str)
      input = StringIO.new(str.force_encoding("binary"))
      Zlib::GzipReader.new(input).read
    end

    def fixture_file_path(filename)
      File.join(File.dirname(__FILE__), 'fixtures', filename)
    end

    def copy_arbitrary_file_to(directory, extension, compressed: false, spawner: VERM_SPAWNER)
      @arbitrary_file = 'binary_file'
      @arbitrary_file += '.gz' if compressed
      copy_fixture_file_to(directory, extension, @arbitrary_file, 'IF', 'P8unS2JIuR6_UZI5pZ0lxWHhfvR2ocOcRAma_lEiA', compressed: compressed, spawner: spawner)
    end

    def copy_zeros_file_to(directory, extension, spawner: VERM_SPAWNER)
      copy_fixture_file_to(directory, extension, 'zeros.gz', 'Ky', 'H8F7BiViUfTKHTut4j6OWoF0Lq3wbcfESzrfpsx7u', compressed: true, spawner: spawner)
    end

    def copy_fixture_file_to(directory, extension, fixture_filename, subdirectory, filename, compressed: false, spawner: VERM_SPAWNER)
      @original_file = File.join(File.dirname(__FILE__), 'fixtures', fixture_filename)
      @subdirectory = subdirectory
      @filename = filename
      @filename = "#{@filename}.#{extension}" if extension
      @location = "/#{directory}/#{@subdirectory}/#{@filename}" # does not get .gz added, even if the file is compressed
      @filename += '.gz' if compressed

      @dest_directory = File.join(spawner.verm_data, directory)
      @dest_subdirectory = File.join(@dest_directory, @subdirectory)
      FileUtils.mkdir(@dest_directory) unless File.directory?(@dest_directory)
      FileUtils.mkdir(@dest_subdirectory) unless File.directory?(@dest_subdirectory)
      FileUtils.cp(@original_file, File.join(@dest_subdirectory, @filename))
    end

    def get(options)
      verm_spawner = options[:verm] || VERM_SPAWNER
      http = Net::HTTP.new(verm_spawner.hostname, verm_spawner.port)
      http.read_timeout = timeout
      
      request = Net::HTTP::Get.new(options[:path])
      options[:headers].each {|k, v| request[k] = v} if options[:headers]
      request['Accept-Encoding'] = options[:accept_encoding] # even if not set, write a nil to disable decode_content
      assert !request.decode_content, "disabling decode_content failed!"

      response = http.start do |connection|
        connection.request(request) do |response|
          yield response if block_given?
        end
      end

      assert_equal options[:expected_response_code] || 200, response.code.to_i, "The response didn't have the expected code"
      assert_equal_or_nil options[:expected_content_type], response.content_type, "The response had an incorrect content-type" if options.has_key?(:expected_content_type)
      assert_equal_or_nil options[:expected_content_length], response.content_length, "The response had an incorrect content-length" if options.has_key?(:expected_content_length)
      assert_equal_or_nil options[:expected_content_encoding], response['content-encoding'], "The response had an incorrect content-encoding" if options.has_key?(:expected_content_encoding)
      assert_equal_or_nil options[:expected_content], response.body, "The response had incorrect content" if options.has_key?(:expected_content)
      
      response
    end

    # Prevent "Use assert_nil if expecting nil... This will fail in MT6" warnings
    def assert_equal_or_nil(expected, actual, message=nil)
      if expected.nil?
        assert_nil actual, message
      else
        assert_equal expected, actual, message
      end
    end

    def expected_filename(location, options = {})
      verm_spawner = options[:verm] || VERM_SPAWNER
      dest_filename = File.expand_path(File.join(verm_spawner.verm_data, location))
      dest_filename += '.' + options[:expected_extension_suffix] if options[:expected_extension_suffix]
      dest_filename
    end

    def fixture_file_data(file)
      File.read(fixture_file_path(file), :mode => 'rb')
    end

    def extension_from(location)
      if location =~ /\.(.+)$/
        $1
      else
        ""
      end
    end
    
    def post_file(options)
      verm_spawner = options[:verm] || VERM_SPAWNER
      file_data = options[:data] || fixture_file_data(options[:file])
      
      if @multipart
        # don't use this in your own apps!  multipart support is only provided to make direct webpage upload demos,
        # and there's no reason to use it in real apps.  we support it here only so we can test the multipage mode.
        request = Net::HTTP::MultipartPost.new(options[:path])
        request.attach 'uploaded_file', file_data, options[:file], options[:type]
        request.form_data = {"test" => "bar"}
      else
        request = Net::HTTP::Post.new(options[:path])
        request.content_type = options[:type]
        request['Content-Encoding'] = options[:encoding] if options[:encoding]
      end
      request['Accept-Encoding'] = options[:accept_encoding] # even if not set, write a nil to disable decode_content
      assert !request.decode_content, "disabling decode_content failed!"
      
      http = Net::HTTP.new(verm_spawner.hostname, verm_spawner.port)
      http.read_timeout = timeout
      response = http.start do |connection|
        if @multipart
          connection.request(request)
        else
          connection.request(request, file_data)
        end
      end
      response.error! unless response.is_a?(Net::HTTPSuccess)
      assert_equal options[:expected_response_code] || 201, response.code.to_i, "The response didn't have the expected code (got #{response.code}, #{response.body.chomp})"

      location = response['location']
      raise "No location was returned, it was supposed to be saved under #{options[:path]}/" if location.nil?
      raise "The location returned was #{location}, but it was supposed to be saved under #{options[:path]}/" if location[0..options[:path].length] != "#{options[:path]}/"
      raise "The location returned was #{location}, but it was supposed to have a #{options[:expected_extension]} extension" if options[:expected_extension] && extension_from(location) != options[:expected_extension]
      dest_filename = expected_filename(location, options)
      raise "Verm supposedly saved the file to #{dest_filename}, but that doesn't exist" unless File.exist?(dest_filename)
      saved_data = File.read(dest_filename, :mode => 'rb')
      raise "The data saved to file doesn't match the original! #{saved_data.inspect} vs. #{file_data.inspect}" unless saved_data == options[:expected_data] || file_data
      location
    end
    
    def put(options, verm_spawner = VERM_SPAWNER)
      file_data = options[:data] || fixture_file_data(options[:file])
      
      request = Net::HTTP::Put.new(options[:path])
      request.content_type = options[:type] if options[:type]
      request['Content-Encoding'] = options[:encoding] if options[:encoding]
      request['Accept-Encoding'] = options[:accept_encoding] # even if not set, write a nil to disable decode_content
      assert !request.decode_content, "disabling decode_content failed!"
      
      http = Net::HTTP.new(verm_spawner.hostname, verm_spawner.port)
      http.read_timeout = timeout
      response = http.start do |connection|
        connection.request(request, file_data)
      end
      response.error! unless response.is_a?(Net::HTTPSuccess)
      response
    end

    def put_file(options, verm_spawner = VERM_SPAWNER)
      file_data = File.read(fixture_file_path(options[:file]), :mode => 'rb')
      response = put(options.merge(:data => file_data), verm_spawner)
      location = response['location']
      raise "The location returned was #{location}, but it was supposed to be the requested location #{options[:path]}" if location != options[:path]
      dest_filename = expected_filename(location, options)
      raise "Verm supposedly saved the file to #{dest_filename}, but that doesn't exist" unless File.exist?(dest_filename)
      saved_data = File.read(dest_filename, :mode => 'rb')
      raise "The data saved to file doesn't match the original! #{saved_data.inspect} vs. #{file_data.inspect}" unless saved_data == file_data
      dest_filename
    end


    def get_statistics(options = {})
      while true
        response = get(options.merge(:path => "/_statistics", :expected_response_code => 200))
        lines = response.body.split(/\n/)
        results = lines.inject({}) {|results, line| name, value = line.split(/ /); results[name.to_sym] = value.to_i; results}

        # all our ruby test code is single-threaded, so if there's more than one connection active in the
        # verm instance at a time, it means the previous request our test code made has been responded to
        # and our test code has moved on but the server hasn't yet finished logging and cleaning up the
        # connection.  we need to wait until we have the finished statistics or else we'll get intermittent
        # test failures.
        return results unless results[:connections_current] > 1
        sleep 0.1
      end
    end

    def calculate_statistics_change(before, after)
      after.inject({}) {|results, (k, v)| results[k] = v - before[k] unless v == before[k]; results}
    end

    def assert_statistics_change(expected_change, options = {})
      before = get_statistics(options)
      yield
      after = get_statistics(options)

      change = calculate_statistics_change(before, after)
      assert_equal expected_change, change
    end

    def assert_statistics_changes(spawners, expected_changes)
      before = spawners.collect {|spawner| get_statistics(:verm => spawner)}
      yield
      changes = spawners.collect.with_index {|spawner, index| calculate_statistics_change(before[index], get_statistics(:verm => spawner))}
      assert_equal expected_changes, changes
    end
  end
end
